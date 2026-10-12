/*
 * ps3recomp - PPU firmware import resolver (.lib.stub patching)
 *
 * On real hardware the PS3 loader walks the module's .lib.stub table and, for
 * each imported function, patches a pointer slot so the in-image import
 * trampoline jumps to the resolved firmware function's OPD. A decrypted EBOOT
 * ships those slots pointing back at the trampoline itself (a sentinel), so the
 * very first firmware call tail-jumps to garbage (`li r12,0` read as a code
 * address) and the boot aborts.
 *
 * This reproduces the loader's import fixup, but routes every import to our HLE
 * bridge instead of a guest firmware module:
 *
 *   - Read sys_prx_param (PT_PROC_PRX_PARAM, captured by the ELF loader) to get
 *     the [libstub_start, libstub_end) range.
 *   - Walk each sceLibStubTable entry: module name, func count, the FNID array
 *     (the NIDs) and the FSTUB array (the trampoline addresses).
 *   - For each import: decode the pointer slot P the trampoline loads from
 *     (`oris r12,r12,HI; lwz r12,DISP(r12)` -> P = (HI<<16)+sign(DISP)),
 *     synthesise an OPD {code=TAG, toc=0} in guest scratch memory, patch
 *     [P] = OPD, and register TAG -> ps3_import_thunk in the function registry.
 *
 * When the lifted trampoline runs, `lwz r0,[OPD]` = TAG, `mtctr r0; bctr` lands
 * in ps3_indirect_call -> ppu_lookup(TAG) -> ps3_import_thunk -> ps3_hle_call.
 * The caller's TOC is preserved by the trampoline's own `std r2,0x28(r1)` and
 * the caller's matching `ld r2,0x28(r1)` after the bl, per the PPC64 ELFv1 ABI.
 *
 * Game-agnostic: works on any decrypted PS3 PPU ELF with a sys_prx_param.
 */
#include "ppu_recomp.h"   /* ppu_context, ppu_register_function */
#include <stdint.h>
#include <stdio.h>

/* The vm_read / vm_write accessors are declared in ppu_recomp.h. */
extern "C" {
void     ppu_register_function(uint64_t addr, void (*fn)(ppu_context*));
void     ps3_hle_call(uint32_t nid, ppu_context* ctx);

/* Set by ppu_load_elf when it sees the PT_PROC_PRX_PARAM segment (0 if none). */
uint32_t ppu_prx_param_va = 0;
}

/* Synthetic OPDs live in a free guest region. Must avoid every other fixed
 * allocator: image (<=~0x009B9808), TLS bump (0x0E000000 up, ppu_sysprx.c),
 * callback stack (0x0D000000 down, boot_main.c), main stack (0x0FF00000 down),
 * mmapper (0x11000000 up). 0x0A000000 sits in the wide gap between the image
 * and those regions. TAGs are pure registry keys (never dereferenced). */
#define OPD_SCRATCH   0x0A000000u
#define HLE_TAG_BASE  0xF0000000u
#define IMPORT_CAP    4096

static uint32_t g_import_nid[IMPORT_CAP];
static uint32_t g_import_count = 0;

/* Every patched import slot routes here; CTR encodes which import (TAG_BASE+4*i)
 * so we can recover the NID and dispatch to the HLE bridge. */
static void ps3_import_thunk(ppu_context* ctx)
{
    uint32_t tag = (uint32_t)ctx->ctr;
    uint32_t i = (tag - HLE_TAG_BASE) >> 2;
    if (i < g_import_count) ps3_hle_call(g_import_nid[i], ctx);
    else ctx->gpr[3] = 0;
}

extern "C" uint32_t ppu_import_count(void) { return g_import_count; }

/* Walk .lib.stub and install the HLE fixups. Returns the number of imports
 * resolved (0 = no sys_prx_param / nothing to do). */
extern "C" uint32_t ppu_resolve_imports(void)
{
    if (!ppu_prx_param_va) { fprintf(stderr, "[imp] no sys_prx_param; skipping import fixup\n"); return 0; }

    uint32_t pp = ppu_prx_param_va;
    uint32_t magic = vm_read32(pp + 4);
    uint32_t libstub_start = vm_read32(pp + 0x18);
    uint32_t libstub_end   = vm_read32(pp + 0x1C);
    fprintf(stderr, "[imp] sys_prx_param @0x%08X magic=0x%08X libstub 0x%08X..0x%08X\n",
            pp, magic, libstub_start, libstub_end);
    if (libstub_start == 0 || libstub_end <= libstub_start) return 0;

    uint32_t modules = 0;
    for (uint32_t va = libstub_start; va < libstub_end; ) {
        uint32_t structsize = vm_read8(va);
        if (structsize < 0x1C) break;                 /* malformed / end */
        uint16_t num_func = vm_read16(va + 6);
        uint32_t modname  = vm_read32(va + 0x10);
        uint32_t fnid_tab = vm_read32(va + 0x14);
        uint32_t fstub_tab= vm_read32(va + 0x18);

        /* module name (for the log) */
        char name[32]; int n = 0;
        for (; n < 31; n++) { uint8_t c = (uint8_t)vm_read8(modname + n); if (!c) break; name[n] = (char)c; }
        name[n] = 0;

        uint32_t resolved = 0;
        for (uint16_t i = 0; i < num_func && g_import_count < IMPORT_CAP; i++) {
            uint32_t nid = vm_read32(fnid_tab + 4u * i);
            uint32_t tramp = vm_read32(fstub_tab + 4u * i);
            if (!tramp) continue;

            /* Decode the pointer slot the trampoline loads from:
             *   insn1 = oris r12,r12,HI    insn2 = lwz r12,DISP(r12)  */
            uint32_t insn1 = vm_read32(tramp + 4);
            uint32_t insn2 = vm_read32(tramp + 8);
            uint32_t hi = (insn1 & 0xFFFFu) << 16;
            int32_t  disp = (int16_t)(insn2 & 0xFFFFu);
            uint32_t slot = hi + (uint32_t)disp;
            if (slot == 0x5194B8u || slot == 0x519488u || slot == 0x51948Cu || slot == 0x5194A8u || slot == 0x5194A4u)
                fprintf(stderr, "[imp] *** heap-init import slot=0x%08X module=%s NID=0x%08X\n", slot, name, nid);
            if (slot == 0) continue;

            uint32_t idx = g_import_count;
            uint32_t opd = OPD_SCRATCH + 8u * idx;
            uint32_t tag = HLE_TAG_BASE + 4u * idx;
            vm_write32(opd, tag);          /* OPD.code = TAG  */
            vm_write32(opd + 4, 0);        /* OPD.toc  = 0    */
            vm_write32(slot, opd);         /* patch the import pointer slot */
            ppu_register_function(tag, ps3_import_thunk);
            g_import_nid[idx] = nid;
            { static int dmp=-1; if(dmp<0){extern char* getenv(const char*); dmp=getenv("PS3_DUMP_IMPORTS")?1:0;}
              if(dmp) fprintf(stderr, "[imp-nid] %-18s nid=0x%08X slot=0x%08X\n", name, nid, slot); }
            g_import_count++;
            resolved++;
        }
        fprintf(stderr, "[imp]   %-18s %2u funcs resolved\n", name, resolved);
        modules++;
        va += structsize;
    }
    fprintf(stderr, "[imp] %u modules, %u imports routed to HLE\n", modules, g_import_count);
    return g_import_count;
}
