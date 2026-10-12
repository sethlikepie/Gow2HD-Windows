#!/usr/bin/env python3
"""Instala o bloco host factory/TYPE15 do GoW2 (o orfao que faltava no lift).

Porque existe
-------------
`patch_factory_snap_ty15_pin.py` e' um VERIFICADOR PURO (zero escritas):
confirma tres marcadores -- "PS3_FACT_SNAP_MAX 128", "ty15_force_pin_freelist"
e "FORCE freelist pin" -- e reporta MISSING quando faltam. Nunca os instalou.
O codigo que ele verifica era uma edicao MANUAL feita dentro do lift
gitignored: medido a 2026-07-25, depois de aplicar a um lift limpo os 74
patch_*.py que entao existiam, os tres marcadores continuavam a 0 -- e nenhum
desses scripts menciona sequer os simbolos ps3_factory_snap_vt /
ps3_type15_note_resolve / k_ty15_pin / ty15_vt_looks_live (grep a zero).

Este script e' o escritor que faltava. O verificador continua a verificar; o
que muda e' que o comportamento passa a existir, e ai' ele passa legitimamente.

O que instala (extraido VERBATIM de recomp_macos_v2, o lift de producao)
-----------------------------------------------------------------------
1. BLOCO de definicoes (ppu_recomp_001.cpp:258-810 na producao, 553 linhas):
   familia ps3_factory_* + ps3_type15_* + os estaticos g_ty15_*/k_ty15_pin e
   `ty15_force_pin_freelist`. E' uma unidade atomica: `ty15_force_pin_freelist`
   e' `static` (nao atravessa TU) e `ps3_type15_freelist_replenish` chama
   `ps3_type15_product_list_reset`, por isso instala-se inteiro ou nada.
   Colocado imediatamente antes de `void func_0039E794` -- o chunk onde essa
   funcao vive e' resolvido em tempo de execucao (o lifter passou de 31 para 7
   chunks; nao se pode assumir "ppu_recomp_001.cpp").
   Nota de link: `ppu_loader.cpp:1299-1301` chama `ps3_type15_block_stomp` de
   dentro do `vm_write32`. Sem este bloco o link do boot_gow2 falha com
   "Undefined symbols: _ps3_type15_block_stomp".

2. Call site de entrada em `func_0039E794` (construct da factory): snapshot do
   vt (`ps3_factory_snap_vt`) + probe [FACTORY] 39E794 enter (gated por
   PS3_TRACE_FACTORY, OFF por default).

3. Call site de saida em `func_0039E794`: probe [FACTORY] 39E794 leave +
   `ps3_factory_snap_product` + `ty15_force_pin_freelist()` +
   `ps3_type15_freelist_replenish()` -- e' este o "call after TYPE15 construct"
   que o verificador exige no marcador 2.

4. Call site de `ps3_type15_note_resolve` em `func_002B0FB4` (resolve do typemap)
   + a declaracao `extern "C"` correspondente. Sem ele `g_ty15_snap` fica a 0
   para sempre e `ty15_force_pin_freelist` devolve 0 na primeira linha: o
   marcador existiria com comportamento zero, que e' precisamente o falso-verde
   que a regra 4 do CLAUDE.md proibe.

O que NAO instala (de proposito)
--------------------------------
- Os call sites de `ps3_factory_repair_vt` / `ps3_factory_freelist_replenish` /
  `ps3_factory_reuse_product` (bloco B71 icallA), `ps3_type15_repair_if_needed`
  e `ps3_type15_product_list_reset` (bloco CB56C attach) e `ps3_type15_pin_free`
  (func_00263318). Vivem dentro de OUTROS blocos orfaos, verificados por
  patch_b71_skip_icallb_reuse.py / patch_type15_cb56c_product.py /
  patch_type15_cc9d0_disc.py -- cada um precisa do seu proprio escritor.
  As definicoes ficam ca' (a unidade e' atomica), os call sites nao.
- A probe intermedia [FACTORY] 39E794 slot_val: nao e' contigua a nenhum dos
  hunks acima e nao pertence ao comportamento verificado.
- `patch_type15_list_preserve.py` reescreve o corpo de
  `ps3_type15_product_list_reset`. O bloco aqui instalado ja' traz a versao
  CLOSE-PRESERVE da producao, por isso esse patch passa a reportar "ja'
  aplicado" em vez de falhar -- nao ha' dupla instalacao.

Como foi gerado
---------------
Programaticamente (script gerador na scratchpad da sessao de 2026-07-25), nunca
a mao: le `recomp_macos_v2/ppu_recomp_001.cpp` e `.../ppu_recomp_003.cpp` e
recorta, por ancoras textuais, (a) o bloco entre o comentario
"/* ---- Type factory 0x15" e o fecho de `ps3_type15_repair_if_needed`, (b) em
`func_0039E794`, o troco entre `gpr[10] = gpr[3] | gpr[3]` e `gpr[0] = ctx->lr`
e o troco entre o restauro do TOC da 2a chamada e `gpr[0] = *(r1+0xA0)`, e (c)
em `func_002B0FB4`, o troco entre `gpr[9] = *(gpr[11])` e a leitura de vt+0x28.
Os textos vao para o ficheiro com repr(), byte a byte. Para regenerar depois de
um novo lift de producao, repetir estas ancoras.

Provado a 2026-07-25 (saidas reais)
-----------------------------------
- lift limpo + todos os patches -> APPLIED; verificador
  patch_factory_snap_ty15_pin.py passa a "ok: lift (7 chunks) markers=3/3", rc=0
- 2a corrida seguida             -> tudo ALREADY, "0 chunks escritos", md5 dos
  dois chunks inalterado
- bloco e 3 hunks instalados     -> 1 ocorrencia cada, byte a byte iguais aos da
  producao (recomp_macos_v2)
- ancora de saida partida a mao  -> rc=2 e ZERO escritas (md5 inalterado)
- bloco isolado com as decls de vm_*: `clang++ -std=c++20 -fsyntax-only -Wall`
  limpo

Nota de arquitectura (futuro)
-----------------------------
`../host_gow2_factory.cpp` e' a copia versionada deste mesmo bloco, ainda NAO
ligada ao build (ver Task 4 de docs/superpowers/plans/2026-07-25-00-rdy0-
desbloquear-relift.md, com `patch_strip_host_block.py` por escrever). No dia em
que esse ficheiro for compilado e linkado, a parte 1 deste script (as
definicoes) tem de ser removida daqui para nao haver simbolo duplicado; as
partes 2-4 (call sites) continuam a fazer falta no lift.

Contrato de rc
--------------
- tudo ja' instalado                 -> ALREADY, rc=0
- instalou o que faltava             -> APPLIED, rc=0 (idempotente: 2a corrida ALREADY)
- funcao alvo ausente, ancora ausente
  ou ancora ambigua (>1 ocorrencia)  -> RECUSA, rc=2 (o lifter mudou de forma;
  revalidar o bloco contra um lift de producao conhecido-bom antes de reaplicar)
- nenhum chunk de lift legivel       -> rc=3

Uso:  patch_factory_block_ty15_install.py [LIFT_DIR_OU_FICHEIRO...]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths            # noqa: E402

FN_FACT = "func_0039E794"      # construct da factory (snap + pin)
FN_RESOLVE = "func_002B0FB4"   # resolve do typemap (note_resolve)

# Marcadores de idempotencia (texto natural do proprio bloco, sem carimbos).
MARK_BLOCK = "/* ---- Type factory 0x15 (SNDX / table idx 0x54 @ 0x868D48) ----"
MARK_HEAD = "ps3_factory_snap_vt(_o, vm_read32(_o))"
MARK_TAIL = "ps3_factory_snap_product(_fo, _pr)"
MARK_NOTE = "ps3_type15_note_resolve((uint32_t)ctx->gpr[0]"

DECL_NOTE = 'extern "C" void ps3_type15_note_resolve(uint32_t idx, uint32_t obj, uint32_t vt);\n'

# Ancoras no corpo GERADO (verificadas contra o lifter de 2026-07-25).
ANCHOR_BLOCK = "void " + FN_FACT + "(ppu_context* ctx) {"
ANCHOR_HEAD = '        ctx->gpr[10] = ctx->gpr[3] | ctx->gpr[3];\n'
ANCHOR_TAIL = '        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xA0);\n'
# O par (r3=r11; r9=*r11) repete-se na funcao (slot VT+0x28 e slot VT+0x48):
# a ancora e' o PAR de linhas, e o hunk entra no meio.
ANCHOR_NOTE_BEFORE = '        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n'
ANCHOR_NOTE_AFTER = '        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x28);\n'
ANCHOR_NOTE_FN = "void " + FN_RESOLVE + "(ppu_context* ctx) {"

# ---- textos extraidos verbatim do lift de producao (recomp_macos_v2) --------
BLOCK = '/* ---- Type factory 0x15 (SNDX / table idx 0x54 @ 0x868D48) ----\n * Root cause (2026-07-22): freelist FREE of the live factory object\n * (func_00263318) then re-alloc as stream/string buffer → *obj becomes\n * ASCII 0x5F436F75 ("_Cou" from R_Perm "_Count…"). Table slot still points\n * at the same EA. Fix: PIN free of the snapped object; block non-live\n * stores to word0 via ps3_type15_block_stomp (vm_write32). REPAIR remains\n * as defensive last-resort only (should stay 0 with pin). */\nstatic uint32_t g_ty15_obj = 0;\nstatic uint32_t g_ty15_vt = 0;\nstatic int g_ty15_snap = 0;\n\nstatic int ty15_vt_looks_live(uint32_t vt) {\n    /* Guest .text/.data OPDs live below ~0x600000 for this EBOOT. */\n    return vt >= 0x10000u && vt < 0x00600000u;\n}\n\n/* General factory vt snap: stream stomp also hits non-TYPE15 factories\n * (e.g. B71 icallA ent=0x400D6808 was vt=0x515700 → 0x02000000).\n * Was 32 — WAD constructs >32 unique factories before B71, so 0x400D6808\n * never entered the table and repair silently failed (vt stayed 0x02000000). */\n#define PS3_FACT_SNAP_MAX 128\nstatic uint32_t g_fact_snap_obj[PS3_FACT_SNAP_MAX];\nstatic uint32_t g_fact_snap_vt[PS3_FACT_SNAP_MAX];\nstatic uint32_t g_fact_snap_p48[PS3_FACT_SNAP_MAX]; /* last good product list hdr */\nstatic uint32_t g_fact_snap_prod[PS3_FACT_SNAP_MAX]; /* last good product ptr */\nstatic int g_fact_snap_n = 0;\nstatic int g_fact_snap_clock = 0;\nstatic int g_fact_snap_lru[PS3_FACT_SNAP_MAX];\n\nstatic int fact_snap_idx(uint32_t obj) {\n    for (int i = 0; i < g_fact_snap_n; i++)\n        if (g_fact_snap_obj[i] == obj) return i;\n    return -1;\n}\n\nextern "C" void ps3_factory_snap_vt(uint32_t obj, uint32_t vt) {\n    if (!obj || !ty15_vt_looks_live(vt)) return;\n    int i = fact_snap_idx(obj);\n    if (i >= 0) {\n        g_fact_snap_vt[i] = vt;\n        g_fact_snap_lru[i] = ++g_fact_snap_clock;\n        return;\n    }\n    if (g_fact_snap_n < PS3_FACT_SNAP_MAX) {\n        i = g_fact_snap_n++;\n        g_fact_snap_obj[i] = obj;\n        g_fact_snap_vt[i] = vt;\n        g_fact_snap_p48[i] = 0;\n        g_fact_snap_prod[i] = 0;\n        g_fact_snap_lru[i] = ++g_fact_snap_clock;\n        return;\n    }\n    /* Table full: replace least-recently used slot (keep TYPE15 pin sticky). */\n    int victim = 0;\n    int best = g_fact_snap_lru[0];\n    for (int j = 1; j < PS3_FACT_SNAP_MAX; j++) {\n        if (g_fact_snap_obj[j] == 0x47D00000u) continue;\n        if (g_fact_snap_lru[j] < best) {\n            best = g_fact_snap_lru[j];\n            victim = j;\n        }\n    }\n    if (g_fact_snap_obj[victim] == 0x47D00000u) {\n        /* all slots pinned somehow — overwrite slot 0 only if not pin */\n        victim = 0;\n        for (int j = 0; j < PS3_FACT_SNAP_MAX; j++)\n            if (g_fact_snap_obj[j] != 0x47D00000u) { victim = j; break; }\n    }\n    g_fact_snap_obj[victim] = obj;\n    g_fact_snap_vt[victim] = vt;\n    g_fact_snap_p48[victim] = 0;\n    g_fact_snap_prod[victim] = 0;\n    g_fact_snap_lru[victim] = ++g_fact_snap_clock;\n}\n\n/* After successful construct: remember product + fo+0x48. */\nextern "C" void ps3_factory_snap_product(uint32_t fo, uint32_t product) {\n    if (!fo || !product) return;\n    if (product < 0x10000u || product >= 0x4F000000u) return;\n    int i = fact_snap_idx(fo);\n    if (i < 0) {\n        uint32_t vt = vm_read32(fo);\n        if (!ty15_vt_looks_live(vt)) return;\n        ps3_factory_snap_vt(fo, vt);\n        i = fact_snap_idx(fo);\n    }\n    if (i < 0) return;\n    g_fact_snap_prod[i] = product;\n    uint32_t p48 = vm_read32(fo + 0x48u);\n    if (p48 >= 0x10000u && p48 < 0x4F000000u)\n        g_fact_snap_p48[i] = p48;\n    else\n        g_fact_snap_p48[i] = product - 4u; /* list hdr convention */\n}\n\nextern "C" int ps3_factory_repair_vt(uint32_t obj) {\n    if (!obj) return 0;\n    uint32_t cur = vm_read32(obj);\n    int i = fact_snap_idx(obj);\n    int did = 0;\n    if (!ty15_vt_looks_live(cur) && i >= 0 && ty15_vt_looks_live(g_fact_snap_vt[i])) {\n        vm_write32(obj, g_fact_snap_vt[i]);\n        { static int _n=0; if(_n++<16)\n            fprintf(stderr,"[FACTORY] REPAIR obj=0x%08X was=0x%08X -> vt=0x%08X\\n",\n              obj, cur, g_fact_snap_vt[i]); }\n        did = 1;\n    }\n    /* Restore +0x48 product list if stomped */\n    if (i >= 0 && g_fact_snap_p48[i]) {\n        uint32_t p48 = vm_read32(obj + 0x48u);\n        if (p48 < 0x10000u || p48 >= 0x4F000000u ||\n            (p48 >= 0x5F000000u && p48 < 0x7F000000u)) {\n            vm_write32(obj + 0x48u, g_fact_snap_p48[i]);\n            { static int _n=0; if(_n++<16)\n                fprintf(stderr,"[FACTORY] REPAIR +48 obj=0x%08X was=0x%08X -> 0x%08X\\n",\n                  obj, p48, g_fact_snap_p48[i]); }\n            did = 1;\n        }\n    }\n    return did;\n}\n\n\n/* product+0x70 is an intrusive circular list (sentinel = product+0x70).\n * func_002A5024 inits both links to self. Reused products often keep a\n * NULL-terminated pool walk (12×0xD8 nodes) that made func_002A4FE4 hang\n * (NULL → poison 0x27182818 forever).\n *\n * M2 (2026-07-23, H1 / CLOSE-PRESERVE): do NOT wipe a valid non-sentinel\n * head. Prefer close-tail preserve (same poison rules as 2A4FE4 CLOSE-TAIL).\n * Empty circular only when head is 0/bad (shells / freelist replenish). */\nstatic int ps3_type15_list_ptr_bad(uint32_t p) {\n    if (p == 0u) return 1;\n    if (p < 0x10000u || p >= 0x4F000000u) return 1;\n    /* poison / non-heap band seen on NULL-terminated pool tails */\n    if (p >= 0x20000000u && p < 0x40000000u) return 1;\n    return 0;\n}\nextern "C" void ps3_type15_product_list_reset(uint32_t prod) {\n    if (prod < 0x10000u || prod >= 0x4F000000u) return;\n    uint32_t sent = prod + 0x70u;\n    uint32_t head = vm_read32(sent);\n    /* Already empty circular? */\n    if (head == sent && vm_read32(sent + 4u) == sent) return;\n\n    /* Head is sentinel but prev stale → just fix prev. */\n    if (head == sent) {\n        vm_write32(sent + 4u, sent);\n        return;\n    }\n\n    /* Head invalid → empty circular (shells, freelist template inherit). */\n    if (ps3_type15_list_ptr_bad(head)) {\n        vm_write32(sent + 0u, sent);\n        vm_write32(sent + 4u, sent);\n        { static int _n = 0;\n          if (_n++ < 16)\n            fprintf(stderr,\n                    "[TYPE15] product list RESET prod=0x%08X was_head=0x%08X "\n                    "-> circular empty (2A4FE4-safe)\\n",\n                    prod, head);\n        }\n        return;\n    }\n\n    /* Valid head: walk next pointers; close broken tail onto sentinel.\n     * Do not empty the whole list (H1 fix — preserve freelist/pool nodes). */\n    {\n        uint32_t cur = head;\n        uint32_t prev = sent;\n        uint32_t nodes = 0u;\n        const uint32_t k_cap = 65536u;\n        for (;;) {\n            if (cur == sent) {\n                /* Already circular. Ensure sent->prev = last node. */\n                if (prev != sent)\n                    vm_write32(sent + 4u, prev);\n                { static int _n = 0;\n                  if (_n++ < 16)\n                    fprintf(stderr,\n                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "\n                            "head=0x%08X closed_at=0x%08X nodes=%u (already circular)\\n",\n                            prod, head, prev, nodes);\n                }\n                return;\n            }\n            if (ps3_type15_list_ptr_bad(cur)) {\n                if (prev == sent) {\n                    vm_write32(sent + 0u, sent);\n                    vm_write32(sent + 4u, sent);\n                    { static int _n = 0;\n                      if (_n++ < 16)\n                        fprintf(stderr,\n                                "[TYPE15] product list RESET prod=0x%08X was_head=0x%08X "\n                                "-> circular empty (bad mid-walk)\\n",\n                                prod, head);\n                    }\n                } else {\n                    vm_write32(prev, sent);\n                    vm_write32(sent + 4u, prev);\n                    { static int _n = 0;\n                      if (_n++ < 16)\n                        fprintf(stderr,\n                                "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "\n                                "head=0x%08X closed_at=0x%08X nodes=%u (bad node)\\n",\n                                prod, head, prev, nodes);\n                    }\n                }\n                return;\n            }\n            uint32_t nx = vm_read32(cur);\n            nodes++;\n            if (nodes >= k_cap) {\n                vm_write32(cur, sent);\n                vm_write32(sent + 4u, cur);\n                { static int _n = 0;\n                  if (_n++ < 16)\n                    fprintf(stderr,\n                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "\n                            "head=0x%08X closed_at=0x%08X nodes=%u (cap)\\n",\n                            prod, head, cur, nodes);\n                }\n                return;\n            }\n            if (ps3_type15_list_ptr_bad(nx)) {\n                /* Close this node onto sentinel (NULL/poison tail). */\n                vm_write32(cur, sent);\n                vm_write32(sent + 4u, cur);\n                { static int _n = 0;\n                  if (_n++ < 16)\n                    fprintf(stderr,\n                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "\n                            "head=0x%08X closed_at=0x%08X nodes=%u\\n",\n                            prod, head, cur, nodes);\n                }\n                return;\n            }\n            prev = cur;\n            cur = nx;\n        }\n    }\n}\n\n/* If factory+0x48 has a live product list, return product (hdr+4). */\nextern "C" uint32_t ps3_factory_reuse_product(uint32_t fo) {\n    if (!fo || fo >= 0x4F000000u) return 0;\n    ps3_factory_repair_vt(fo);\n    uint32_t hdr = vm_read32(fo + 0x48u);\n    uint32_t prod = 0;\n    if (hdr >= 0x10000u && hdr < 0x4F000000u)\n        prod = hdr + 4u;\n    /* Fall back to snapped product if +48 still bad */\n    if (prod < 0x10000u || prod >= 0x4F000000u) {\n        int i = fact_snap_idx(fo);\n        if (i >= 0 && g_fact_snap_prod[i]) {\n            prod = g_fact_snap_prod[i];\n            hdr = g_fact_snap_p48[i] ? g_fact_snap_p48[i] : (prod - 4u);\n            vm_write32(fo + 0x48u, hdr);\n        }\n    }\n    if (prod < 0x10000u || prod >= 0x4F000000u) return 0;\n    { static int _n=0; if(_n++<16)\n        fprintf(stderr,"[FACTORY] reuse product fo=0x%08X hdr=0x%08X prod=0x%08X\\n",\n          fo, hdr, prod); }\n    return prod;\n}\n\n/* Replenish freelist at fo+0x24 using a dedicated pin slab per factory.\n * Layout matches TYPE15 success: *FL=FL+0x18, *(FL+0x18)=shell+4, *(FL+4)=1. */\nextern "C" int ps3_factory_freelist_replenish(uint32_t fo, uint16_t type_id) {\n    if (!fo || fo >= 0x4F000000u) return 0;\n    ps3_factory_repair_vt(fo);\n    /* Pin slab below Spurs (0x47C04080) and TYPE15 pin (0x47D00000).\n     * Was 0x47C00000 — collided with CreateTaskset/spurs and broke early boot. */\n    uint32_t slab = 0x47A00000u + ((fo >> 8) & 0x1FFu) * 0x800u;\n    if (slab < 0x47A00000u || slab >= 0x47C00000u) slab = 0x47A00000u;\n    uint32_t fl = slab;\n    uint32_t shell = slab + 0x400u;\n    for (uint32_t off = 0; off < 0x800u; off += 4u)\n        vm_write32(slab + off, 0u);\n    /* Template from existing product if any */\n    uint32_t prod = 0, pvt = 0;\n    uint32_t hdr = vm_read32(fo + 0x48u);\n    if (hdr >= 0x10000u && hdr < 0x4F000000u) {\n        prod = hdr + 4u;\n        pvt = vm_read32(prod);\n        if (!ty15_vt_looks_live(pvt)) {\n            for (uint32_t o = 0; o < 0x40u; o += 4u) {\n                uint32_t c = vm_read32(prod + o);\n                if (ty15_vt_looks_live(c)) { pvt = c; break; }\n            }\n        }\n        if (prod) {\n            for (uint32_t o = 0; o < 0x80u; o += 4u)\n                vm_write32(shell + o, vm_read32(prod + o));\n        }\n    }\n    if (ty15_vt_looks_live(pvt))\n        vm_write32(shell, pvt);\n    if (type_id)\n        vm_write16(shell + 2u, type_id);\n    uint32_t mid = fl + 0x18u;\n    vm_write32(fl + 0u, mid);\n    vm_write32(fl + 4u, 1u);\n    vm_write32(mid, shell + 4u);\n    vm_write32(fo + 0x24u, fl);\n    { static int _n=0; if(_n++<16)\n        fprintf(stderr,"[FACTORY] freelist REPLENISH fo=0x%08X fl=0x%08X shell=0x%08X type=0x%04X pvt=0x%08X\\n",\n          fo, fl, shell, type_id, pvt); }\n    return 1;\n}\n\nextern "C" uint32_t ps3_type15_protected_obj(void) {\n    return (g_ty15_snap && g_ty15_obj) ? g_ty15_obj : 0u;\n}\nextern "C" uint32_t ps3_type15_good_vt(void) {\n    return g_ty15_vt;\n}\n/* Called from vm_write32: refuse stomping factory word0 with non-live data. */\nextern "C" int ps3_type15_block_stomp(uint32_t addr, uint32_t val) {\n    if (!g_ty15_snap || !g_ty15_obj) return 0;\n    if (addr != g_ty15_obj) return 0;\n    if (ty15_vt_looks_live(val) || val == g_ty15_vt) return 0; /* allow good vt */\n    /* Free marks low bit (vt|1) — also refuse; pin free instead. */\n    { static int _n=0; if(_n++<32)\n        fprintf(stderr,"[TYPE15] STOMP-BLOCK obj=0x%08X would_write=0x%08X (keep vt=0x%08X)\\n",\n          g_ty15_obj, val, g_ty15_vt); }\n    return 1; /* block */\n}\n/* Free pin: skip freelist free of factory object (func_00263318). */\nextern "C" int ps3_type15_pin_free(uint32_t blk) {\n    if (!g_ty15_snap || !g_ty15_obj || blk != g_ty15_obj) return 0;\n    { static int _n=0; if(_n++<16)\n        fprintf(stderr,"[TYPE15] PIN-FREE skip free of factory obj=0x%08X vt=0x%08X\\n",\n          g_ty15_obj, g_ty15_vt); }\n    return 1;\n}\n\n/* Re-home factory to a pin zone outside freelist churn (0x40100000 arena).\n * Original obj at ~0x401002F0 is bulk-stomped (memcpy/stream) with "_Cou";\n * table must point at an EA that never receives that body write. */\n/* Inside sys_memory window 0x40100000+0x7D00000 → end 0x47E00000; stay below. */\nstatic const uint32_t k_ty15_pin = 0x47D00000u;\nstatic int g_ty15_rehomed = 0;\n\nextern "C" void ps3_type15_note_resolve(uint32_t idx, uint32_t obj, uint32_t vt) {\n    if (idx != 0x54u || !obj) return;\n    if (ty15_vt_looks_live(vt)) {\n        if (!g_ty15_snap || (g_ty15_obj != obj && g_ty15_obj != k_ty15_pin)) {\n            g_ty15_obj = obj;\n            g_ty15_vt = vt;\n            g_ty15_snap = 1;\n            { static int _n=0; if(_n++<8)\n                fprintf(stderr,"[TYPE15] SNAP obj=0x%08X vt=0x%08X\\n", obj, vt); }\n            /* REHOME once: copy object bytes to pin zone, retarget table slot.\n             * Also rehome freelist at +0x24 — it lives next to the original\n             * factory in 0x401xxxxx and gets stream-stomped to ASCII\n             * ("_Capacity…") after first construct, so CB56C 2nd construct\n             * sees slot_val=0. Copy freelist blob to pin+0x400 and retarget. */\n            if (!g_ty15_rehomed && obj != k_ty15_pin && obj >= 0x40000000u) {\n                uint32_t tab = 0x00868D48u;\n                const uint32_t k_fl_pin = k_ty15_pin + 0x400u;\n                /* Copy ~0x200 of factory object (covers +0xD4/+0xC8 tables). */\n                for (uint32_t off = 0; off < 0x200u; off += 4u)\n                    vm_write32(k_ty15_pin + off, vm_read32(obj + off));\n                /* Ensure live vt on pin. */\n                vm_write32(k_ty15_pin, vt);\n                /* Freelist rehome: +0x24 points into stomped arena. */\n                {\n                    uint32_t fl = vm_read32(k_ty15_pin + 0x24u);\n                    if (fl >= 0x40000000u && fl < 0x47D00000u) {\n                        for (uint32_t off = 0; off < 0x200u; off += 4u)\n                            vm_write32(k_fl_pin + off, vm_read32(fl + off));\n                        vm_write32(k_ty15_pin + 0x24u, k_fl_pin);\n                        { static int _n=0; if(_n++<8) {\n                            fprintf(stderr,"[TYPE15] REHOME freelist old=0x%08X pin=0x%08X "\n                              "*fl=0x%08X fl+4=0x%08X\\n",\n                              fl, k_fl_pin, vm_read32(k_fl_pin),\n                              vm_read32(k_fl_pin + 4u));\n                            fprintf(stderr,"[TYPE15] freelist words:");\n                            for (uint32_t i=0;i<16;i++)\n                              fprintf(stderr," %08X", vm_read32(k_fl_pin + i*4u));\n                            fprintf(stderr,"\\n");\n                          } }\n                    }\n                }\n                /* Table idx 0x54 → pin. */\n                if (vm_read32(tab + 0x54u) == obj)\n                    vm_write32(tab + 0x54u, k_ty15_pin);\n                g_ty15_obj = k_ty15_pin;\n                g_ty15_rehomed = 1;\n                { static int _n=0; if(_n++<8)\n                    fprintf(stderr,"[TYPE15] REHOME old=0x%08X pin=0x%08X tab[0x54]=0x%08X vt=0x%08X "\n                      "+24=0x%08X +44=0x%08X +48=0x%08X +C8b=%d +D4=0x%08X\\n",\n                      obj, k_ty15_pin, vm_read32(tab + 0x54u), vt,\n                      vm_read32(k_ty15_pin+0x24), vm_read32(k_ty15_pin+0x44),\n                      vm_read32(k_ty15_pin+0x48),\n                      (int)(int8_t)vm_read8(k_ty15_pin+0xC8),\n                      vm_read32(k_ty15_pin+0xD4)); }\n            }\n        }\n        return;\n    }\n    /* Defensive only if rehome/pin failed and table still points at stomped EA. */\n    if (g_ty15_snap && g_ty15_vt) {\n        uint32_t ent = obj;\n        uint32_t was = vm_read32(ent);\n        if (!ty15_vt_looks_live(was) && g_ty15_rehomed) {\n            /* Prefer retarget table to pin rather than REPAIR stomped word. */\n            uint32_t tab = 0x00868D48u;\n            if (vm_read32(tab + 0x54u) == ent) {\n                vm_write32(tab + 0x54u, k_ty15_pin);\n                { static int _n=0; if(_n++<16)\n                    fprintf(stderr,"[TYPE15] RETARGET tab[0x54] stomped=0x%08X -> pin=0x%08X\\n",\n                      ent, k_ty15_pin); }\n                return;\n            }\n        }\n        if (g_ty15_obj == ent && was != g_ty15_vt) {\n            vm_write32(ent, g_ty15_vt);\n            { static int _n=0; if(_n++<32)\n                fprintf(stderr,"[TYPE15] REPAIR obj=0x%08X was_vt=0x%08X -> vt=0x%08X\\n",\n                  ent, was, g_ty15_vt); }\n        }\n    }\n}\n\n/* Force freelist into pin zone. Guest construct often rewrites fo+0x24 to\n * 0x401xxxxx after first product; that arena is stream-stomped before CB56C,\n * so slot_val=0 and construct returns null. Always keep FL at pin+0x400. */\nstatic int ty15_force_pin_freelist(void) {\n    if (!g_ty15_snap || !g_ty15_rehomed) return 0;\n    uint32_t fo = k_ty15_pin;\n    uint32_t fl = vm_read32(fo + 0x24u);\n    const uint32_t k_fl_pin = k_ty15_pin + 0x400u;\n    if (fl >= 0x47D00000u && fl < 0x47E00000u)\n        return 0; /* already in pin zone */\n    /* Copy whatever is still readable, then retarget. */\n    if (fl >= 0x10000u && fl < 0x4F000000u) {\n        for (uint32_t off = 0; off < 0x200u; off += 4u)\n            vm_write32(k_fl_pin + off, vm_read32(fl + off));\n    }\n    vm_write32(fo + 0x24u, k_fl_pin);\n    { static int _n=0; if(_n++<16)\n        fprintf(stderr,"[TYPE15] FORCE freelist pin old=0x%08X -> 0x%08X\\n",\n          fl, k_fl_pin); }\n    return 1;\n}\n\n/* Replenish TYPE15 freelist with a fresh shell in pin zone.\n * Original freelist entries live in 0x401xxxxx and get stream-stomped.\n * Layout (from first in-boot success): *FL = FL+0x18, *(FL+0x18) = shell+4, *(FL+4)=count. */\nextern "C" int ps3_type15_freelist_replenish(void) {\n    if (!g_ty15_snap || !g_ty15_rehomed) return 0;\n    ty15_force_pin_freelist();\n    uint32_t fo = k_ty15_pin;\n    uint32_t fl = vm_read32(fo + 0x24u);\n    if (fl < 0x47D00000u || fl >= 0x47E00000u) {\n        fl = k_ty15_pin + 0x400u;\n        vm_write32(fo + 0x24u, fl);\n    }\n    uint32_t head = vm_read32(fl);\n    uint32_t count = vm_read32(fl + 4u);\n    /* Need replenish if count==0 or head/slot look stomped (ASCII 0x5F..) or null. */\n    int need = 0;\n    if (count == 0u) need = 1;\n    else if (head < 0x10000u || head >= 0x4F000000u) need = 1;\n    else {\n        uint32_t slot = vm_read32(head);\n        if (slot < 0x10000u || slot >= 0x4F000000u || (slot >= 0x5F000000u && slot < 0x7F000000u))\n            need = 1;\n        if (head >= 0x5F000000u && head < 0x7F000000u) need = 1;\n    }\n    if (!need) return 0;\n    const uint32_t shell = k_ty15_pin + 0x800u;\n    for (uint32_t off = 0; off < 0x400u; off += 4u)\n        vm_write32(shell + off, 0u);\n    /* Product-class vt from first live TYPE15 product. +48 is list HEADER\n     * (product-4); product object starts at hdr+4. Also stamp type 0x15 at +2\n     * so CB56C icall2 indexes factory table correctly. */\n    {\n        uint32_t pvt = 0;\n        uint32_t prod = 0;\n        uint32_t hdr = vm_read32(k_ty15_pin + 0x48u);\n        if (hdr >= 0x10000u && hdr < 0x4F000000u) {\n            prod = hdr + 4u;\n            pvt = vm_read32(prod);\n            /* If product word0 not a live vt, try scanning +0..0x20 for one. */\n            if (!ty15_vt_looks_live(pvt)) {\n                for (uint32_t o = 0; o < 0x20u; o += 4u) {\n                    uint32_t c = vm_read32(prod + o);\n                    if (ty15_vt_looks_live(c)) { pvt = c; break; }\n                }\n            }\n        }\n        if (ty15_vt_looks_live(pvt)) {\n            vm_write32(shell, pvt);\n            /* Copy a few words of product template so construct has state. */\n            if (prod) {\n                for (uint32_t o = 4; o < 0x40u; o += 4u)\n                    vm_write32(shell + o, vm_read32(prod + o));\n            }\n            /* TYPE15 type id at +2 (CB56C icall2 uses this). */\n            vm_write16(shell + 2u, 0x0015u);\n            /* Do not inherit a dirty +0x70 list from the template product. */\n            ps3_type15_product_list_reset(shell);\n        }\n        { static int _n=0; if(_n++<8)\n            fprintf(stderr,"[TYPE15] shell product vt=0x%08X prod_src=0x%08X +2=0x%04X\\n",\n              pvt, prod, vm_read16(shell+2u)); }\n    }\n    uint32_t mid = fl + 0x18u;\n    vm_write32(fl + 0u, mid);\n    vm_write32(fl + 4u, 1u);\n    vm_write32(mid, shell + 4u);\n    { static int _n=0; if(_n++<16)\n        fprintf(stderr,"[TYPE15] freelist REPLENISH fl=0x%08X mid=0x%08X shell=0x%08X count=1\\n",\n          fl, mid, shell); }\n    return 1;\n}\n\nextern "C" int ps3_type15_repair_if_needed(uint32_t obj) {\n    if (!g_ty15_snap || !obj) return 0;\n    if (g_ty15_obj && obj != g_ty15_obj) return 0;\n    /* Always try freelist replenish for TYPE15 pin before construct. */\n    if (obj == k_ty15_pin || obj == g_ty15_obj)\n        ps3_type15_freelist_replenish();\n    uint32_t vt = vm_read32(obj);\n    if (ty15_vt_looks_live(vt)) {\n        /* Freelist head at +0x24 must not be ASCII/stomped (0x5F... = \'_\'). */\n        uint32_t fl = vm_read32(obj + 0x24u);\n        uint32_t head = (fl >= 0x10000u && fl < 0x4F000000u) ? vm_read32(fl) : 0u;\n        if (head >= 0x5F000000u && head < 0x7F000000u) {\n            /* Prefer pin freelist at pin+0x400 if we rehomed. */\n            uint32_t k_fl_pin = k_ty15_pin + 0x400u;\n            uint32_t pin_head = vm_read32(k_fl_pin);\n            if (pin_head && pin_head < 0x5F000000u) {\n                vm_write32(obj + 0x24u, k_fl_pin);\n                { static int _n=0; if(_n++<16)\n                    fprintf(stderr,"[TYPE15] REPAIR freelist head was_ascii=0x%08X -> pin_fl=0x%08X head=0x%08X\\n",\n                      head, k_fl_pin, pin_head); }\n                return 1;\n            }\n        }\n        return 0;\n    }\n    if (!g_ty15_vt) return 0;\n    vm_write32(obj, g_ty15_vt);\n    { static int _n=0; if(_n++<32)\n        fprintf(stderr,"[TYPE15] REPAIR-precall obj=0x%08X was=0x%08X -> 0x%08X\\n",\n          obj, vt, g_ty15_vt); }\n    return 1;\n}\n'

HUNK_HEAD = '        { uint32_t _o=(uint32_t)ctx->gpr[10];\n          if (_o >= 0x10000u && _o < 0x4F000000u)\n            ps3_factory_snap_vt(_o, vm_read32(_o)); }\n        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n            const char* e=getenv("PS3_TRACE_FACTORY"); _on=(e&&*e&&*e!=\'0\')?1:0;}\n          if(_on){ static int _n=0;\n            uint32_t fo=(uint32_t)ctx->gpr[10];\n            uint32_t d=(uint32_t)ctx->gpr[4];\n            uint32_t vt0=vm_read32(fo);\n            int force = (fo==0x47D00000u || vt0==0x00516D70u);\n            if(force || _n++<64){\n            fprintf(stderr,"[FACTORY] 39E794 enter this=0x%08X desc=0x%08X vt=0x%08X "\n              "+24=0x%08X +44=0x%08X +48=0x%08X +50=0x%08X +D4=0x%08X "\n              "+C8b=%d d0=0x%08X d2=0x%04X\\n",\n              fo, d, vt0,\n              vm_read32(fo+0x24), vm_read32(fo+0x44), vm_read32(fo+0x48),\n              vm_read32(fo+0x50), vm_read32(fo+0xD4),\n              (int)(int8_t)vm_read8(fo+0xC8),\n              d?vm_read32(d):0u, d?vm_read16(d+2):0u);\n            fflush(stderr);} } }\n'

HUNK_TAIL = '        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n            const char* e=getenv("PS3_TRACE_FACTORY"); _on=(e&&*e&&*e!=\'0\')?1:0;}\n          if(_on){ static int _n=0;\n            uint32_t fo=(uint32_t)ctx->gpr[29];\n            int force=(fo==0x47D00000u || vm_read32(fo)==0x00516D70u);\n            if(force || _n++<64) {\n            fprintf(stderr,"[FACTORY] 39E794 leave this=0x%08X product r3=0x%08X\\n",\n              fo, (uint32_t)ctx->gpr[3]);\n            if (force) {\n              uint32_t fl=vm_read32(fo+0x24u);\n              uint32_t pr=(uint32_t)ctx->gpr[3];\n              fprintf(stderr,"[TYPE15] post-construct fl=0x%08X product=0x%08X words:", fl, pr);\n              for (uint32_t i=0;i<12 && fl;i++)\n                fprintf(stderr," %08X", vm_read32(fl + i*4u));\n              fprintf(stderr,"\\n");\n              if (pr >= 0x10000u && pr < 0x4F000000u) {\n                fprintf(stderr,"[TYPE15] product dump:");\n                for (uint32_t i=0;i<12;i++)\n                  fprintf(stderr," %08X", vm_read32(pr + i*4u));\n                fprintf(stderr," +2u16=0x%04X\\n", vm_read16(pr+2u));\n              }\n            }\n            fflush(stderr);} } }\n        /* Always snap successful products (not TRACE-gated) for B71/CB56C reuse. */\n        { uint32_t _fo = (uint32_t)ctx->gpr[29];\n          uint32_t _pr = (uint32_t)ctx->gpr[3];\n          if (_fo >= 0x10000u && _fo < 0x4F000000u\n              && _pr >= 0x10000u && _pr < 0x4F000000u)\n            ps3_factory_snap_product(_fo, _pr);\n          /* TYPE15: after first live product, guest often rewrites +0x24 to\n           * 0x401xxxxx freelist which stream stomps before CB56C. Pin it. */\n          if (_fo == 0x47D00000u || vm_read32(_fo) == 0x00516D70u) {\n            extern int ps3_type15_freelist_replenish(void);\n            /* force pin + replenish if empty (may no-op if count>0 in pin). */\n            ty15_force_pin_freelist();\n            ps3_type15_freelist_replenish();\n          }\n        }\n'

HUNK_NOTE = '        /* Type-0x15 factory: snapshot live vt; repair if stomped (SNDX path). */\n        /* Snapshot live type-0x15 factory; do NOT repair+re-enter expand here —\n         * object may be mid-stomp. Repair is for CB56C post-WAD only. */\n        { ps3_type15_note_resolve((uint32_t)ctx->gpr[0], (uint32_t)ctx->gpr[11], (uint32_t)ctx->gpr[9]); }\n'


class Recusa(Exception):
    """Ancora ausente ou ambigua: o lift nao tem a forma esperada."""


def region_of(text: str, fn: str) -> "tuple[int, int]":
    """Intervalo [i, j) do corpo da funcao fn dentro de text."""
    i = text.find("void %s(ppu_context* ctx) {" % fn)
    if i < 0:
        return -1, -1
    j = text.find("\nvoid func_", i + 10)
    return i, (j + 1 if j > i else len(text))


def _once(region: str, anchor: str, what: str) -> int:
    n = region.count(anchor)
    if n != 1:
        raise Recusa("%s: ancora com %d ocorrencias (esperado 1)" % (what, n))
    return region.index(anchor)


def plan_factory_chunk(path: str, t: str) -> "tuple[str, list[str]]":
    """Bloco de definicoes + os 2 hunks dentro de func_0039E794. Nao escreve."""
    done = []

    if MARK_BLOCK not in t:
        k = _once(t, ANCHOR_BLOCK, "bloco host (" + ANCHOR_BLOCK + ")")
        t = t[:k] + BLOCK + "\n" + t[k:]
        done.append("bloco host instalado (%d linhas)" % BLOCK.count("\n"))
    else:
        done.append("bloco host ALREADY")

    i, j = region_of(t, FN_FACT)
    if i < 0:
        raise Recusa("%s ausente no chunk" % FN_FACT)
    region = t[i:j]

    if MARK_HEAD not in region:
        k = _once(region, ANCHOR_HEAD, "snap_vt (entrada de %s)" % FN_FACT)
        region = region[:k + len(ANCHOR_HEAD)] + HUNK_HEAD + region[k + len(ANCHOR_HEAD):]
        done.append("call site ps3_factory_snap_vt instalado")
    else:
        done.append("call site ps3_factory_snap_vt ALREADY")

    if MARK_TAIL not in region:
        k = _once(region, ANCHOR_TAIL, "snap_product/force_pin (saida de %s)" % FN_FACT)
        region = region[:k] + HUNK_TAIL + region[k:]
        done.append("call site ps3_factory_snap_product + ty15_force_pin_freelist instalado")
    else:
        done.append("call site ps3_factory_snap_product + ty15_force_pin_freelist ALREADY")

    return t[:i] + region + t[j:], done


def plan_resolve_chunk(path: str, t: str) -> "tuple[str, list[str]]":
    """Declaracao + call site de ps3_type15_note_resolve. Nao escreve."""
    done = []

    i, j = region_of(t, FN_RESOLVE)
    if i < 0:
        raise Recusa("%s ausente no chunk" % FN_RESOLVE)
    region = t[i:j]

    if MARK_NOTE not in region:
        pair = ANCHOR_NOTE_BEFORE + ANCHOR_NOTE_AFTER
        k = _once(region, pair, "note_resolve (slot VT+0x28 de %s)" % FN_RESOLVE)
        k += len(ANCHOR_NOTE_BEFORE)
        region = region[:k] + HUNK_NOTE + region[k:]
        done.append("call site ps3_type15_note_resolve instalado")
    else:
        done.append("call site ps3_type15_note_resolve ALREADY")
    t = t[:i] + region + t[j:]

    if DECL_NOTE not in t:
        k = _once(t, ANCHOR_NOTE_FN, "decl note_resolve")
        t = t[:k] + DECL_NOTE + t[k:]
        done.append("decl extern ps3_type15_note_resolve instalada")
    else:
        done.append("decl extern ps3_type15_note_resolve ALREADY")

    return t, done


def _write(path: Path, text: str) -> None:
    try:
        path.write_text(text, encoding="utf-8", newline="\n")
    except TypeError:                      # python < 3.10
        path.write_text(text, encoding="utf-8")


def _holder(paths, fn):
    sig = "void %s(ppu_context* ctx) {" % fn
    return [p for p in paths if sig in p.read_text(encoding="utf-8", errors="replace")]


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], "ppu_recomp_001.cpp")
             if p.is_file()]
    if not paths:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 3

    fact = _holder(paths, FN_FACT)
    resolve = _holder(paths, FN_RESOLVE)
    if len(fact) != 1:
        print("ERRO: %s encontrado em %d chunks (esperado 1)"
              % (FN_FACT, len(fact)), file=sys.stderr)
        return 2
    if len(resolve) != 1:
        print("ERRO: %s encontrado em %d chunks (esperado 1)"
              % (FN_RESOLVE, len(resolve)), file=sys.stderr)
        return 2

    # Duas fases: planear TUDO (pode levantar Recusa) e so' depois escrever.
    # Assim uma ancora partida nunca deixa o lift meio-patcheado.
    try:
        pend = []
        for p, planner in ((fact[0], plan_factory_chunk),
                           (resolve[0], plan_resolve_chunk)):
            orig = p.read_text(encoding="utf-8", errors="replace")
            new, done = planner(p.name, orig)
            pend.append((p, orig, new, done))
    except Recusa as e:
        print("ERRO: %s.\n"
              "  O corpo gerado nao e' o esperado -- o lifter mudou de forma.\n"
              "  Nada foi escrito. Revalida o bloco contra um lift de producao\n"
              "  conhecido-bom (recomp_macos_v2) e regenera este script." % e,
              file=sys.stderr)
        return 2

    changed = 0
    for p, orig, new, done in pend:
        if new != orig:
            _write(p, new)
            changed += 1
        for line in done:
            print("  %s: %s" % (p.name, line))

    print("[factory-ty15-block] ok (%d chunks escritos)" % changed)
    return 0


if __name__ == "__main__":
    sys.exit(main())
