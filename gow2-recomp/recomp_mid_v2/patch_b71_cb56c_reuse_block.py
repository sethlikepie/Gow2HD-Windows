#!/usr/bin/env python3
"""Instala os blocos orfaos B71 (icallA reuse / skip icallB) e CB56C (prefer real product).

Porque existe
-------------
`patch_b71_skip_icallb_reuse.py` e' um VERIFICADOR PURO (zero escritas): confirma
4 marcadores e devolve rc!=0 quando faltam. Faltavam sempre num lift limpo porque
o comportamento que ele verifica e' uma edicao MANUAL feita dentro do lift
gitignored -- codigo sem dono, que NENHUM script versionado repunha. Provado em
2026-07-25 aplicando os 74 patches a um lift limpo: os 4 marcadores ficavam a 0.

Este ficheiro e' o ESCRITOR que faltava. O verificador continua a verificar.

O que instala (extraido verbatim do lift de producao `recomp_macos_v2`)
----------------------------------------------------------------------
1) `func_000B71B8` (chunk com a funcao) -- substituicao do CORPO INTEIRO:
   - `static int g_b71_product_reused = 0;` imediatamente antes da funcao;
   - guarda do icallA da factory (vt/opd/code validados antes do
     `ps3_indirect_call`) + `ps3_factory_repair_vt` / `..._freelist_replenish`;
   - salvamento do produto por `ps3_factory_reuse_product(fo+0x48)` quando o
     construct devolve 0 -> `g_b71_product_reused = 1`;
   - SKIP do icallB de re-attach quando o produto foi salvo por reuse
     (marcador "[POSTINTRO] B71 skip icallB (reuse product");
   - gate HLE-lite do `func_000393E0` (carimba so' a vtable; o registry
     completo fica opt-in por `PS3_B71_FULL_393E0=1`) -- faz parte da MESMA
     edicao manual da funcao, sem escritor proprio, e sem ele o corpo de
     producao nao existe tal como esta';
   - guardas `>= 0x10000u` nos vm_write32 e beacons `[POSTINTRO] B71 ...`.

2) `func_000CB56C` -- INSERCAO do bloco "TYPE15 prefer real product"
   (`PS3_TYPE15_CB56C`, `was_shell=%d`) imediatamente antes do par
   `rldicl/or` que recolhe o produto do icall1. E' a versao evoluida do bloco
   pequeno de `patch_type15_cb56c_highbit.py`: esse corre depois (ordem
   alfabetica) e fica ALREADY porque a sua agulha `[TYPE15] CB56C reuse product`
   ja' esta' presente. O bloco `[TYPE15] CB56C type high-bit` continua a ser
   dele -- NAO e' instalado aqui.

O que NAO instala (tem escritor proprio -- nao duplicar)
--------------------------------------------------------
   POSTTHR-PROBE id=3 .......... patch_postthr_pc_probe.py
   MENUPRESENT-PROBE id=12 ..... patch_menu_present_schedule_probe.py
   FLIPPATH-PROBE id=2 ......... patch_flip_path_enter_probe.py
   INTROSEQ-PROBE (beacon) ..... patch_introseq_probe.py
   [TYPE15] CB56C type high-bit  patch_type15_cb56c_highbit.py
Todos correm DEPOIS deste script e reinserem-se sozinhos.

Dependencias que este script NAO resolve (declaradas, nunca escondidas)
-----------------------------------------------------------------------
O bloco do B71 chama `ps3_factory_repair_vt`, `ps3_factory_reuse_product` e
`ps3_factory_freelist_replenish`. Essas tres sao INJECCOES DE PREAMBULO (host,
escritas a mao no lift), inventariadas em `lift_baseline/MANIFEST.tsv` com copia
versionada em `lift_baseline/injected_000.cpp` / `injected_001.cpp` e em
`../host_gow2_factory.cpp`. Nenhum patch_*.py as instala -- e' uma familia de
orfaos separada, com dono proprio. Este script AVISA quando faltam (nao falha
por isso: o bloco de comportamento e' o que aqui se repoe), pelo mesmo criterio
do precedente `patch_ce03c_introseq_block.py`, cujo bloco tambem depende de
simbolos de preambulo (`ppu_giant_lock_release`, `usleep`).

Como foi gerado
---------------
Nao foi transcrito a mao. Um gerador leu os dois lifts (limpo+patches anteriores
e producao), diffou `func_000B71B8` / `func_000CB56C`, removeu do corpo de
producao as probes com escritor proprio e emitiu este ficheiro com os corpos
embebidos via `repr()`.

Nota sobre o codegen
--------------------
O corpo instalado e' o de PRODUCAO verbatim, portanto traz o codegen do lifter
que o gerou (sem `ctx->lr = 0x...` antes das chamadas, callee-save por
`vm_read64` do stack guest em vez dos temporarios `_cs_NN`). E' o corpo provado
in-boot com este bloco; o corpo novo do lifter nunca o foi. Por isso o contrato
abaixo RECUSA em vez de adivinhar quando o lifter mudar de forma.

Contrato de rc
--------------
  rc=0  aplicado, ou ja' aplicado (ALREADY)
  rc=1  nenhum chunk tem `func_000B71B8`/`func_000CB56C` (lift errado)
  rc=2  a funcao existe mas o corpo gerado NAO e' o esperado -> RECUSA.
        O lifter mudou: revalidar o bloco contra um lift de producao
        conhecido-bom e regerar este script. Nunca substituir as cegas.

Uso:  patch_b71_cb56c_reuse_block.py [LIFT_DIR_OU_FICHEIRO...]
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lift_paths import resolve_lift_paths            # noqa: E402

SIG_B71 = 'void func_000B71B8(ppu_context* ctx) {\n'
DECL_B71 = '/* B71: when icallA salvages product from fo+0x48, skip icallB re-attach. */\nstatic int g_b71_product_reused = 0;\n\n'
MARKER_B71 = "[POSTINTRO] B71 skip icallB (reuse product"

# Corpo GERADO que este patch espera encontrar (lifter de 2026-07-25).
CLEAN_BODY_B71 = 'void func_000B71B8(ppu_context* ctx) {\n        uint64_t _cs_28 = ctx->gpr[28];\n        uint64_t _cs_29 = ctx->gpr[29];\n        uint64_t _cs_30 = ctx->gpr[30];\n        uint64_t _cs_31 = ctx->gpr[31];\n        vm_write64(ctx->gpr[1] + -0x110, ctx->gpr[1]); ctx->gpr[1] += -0x110;\n        ctx->gpr[0] = ctx->lr;\n        vm_write64(ctx->gpr[1] + 0x100, ctx->gpr[30]);\n        vm_write64(ctx->gpr[1] + 0x120, ctx->gpr[0]);\n        vm_write64(ctx->gpr[1] + 0xF0, ctx->gpr[28]);\n        vm_write64(ctx->gpr[1] + 0xF8, ctx->gpr[29]);\n        vm_write64(ctx->gpr[1] + 0x108, ctx->gpr[31]);\n        ctx->lr = 0x000B71D8; func_002BA4B4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B71E0; func_002B6C40(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B71E8; func_002B7644(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B71F0; func_0004476C(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B71F8; func_000CE0A0(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7200; func_00040090(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61D8);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61DC);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        ctx->lr = 0x000B7214; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61D0);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61D4);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        ctx->lr = 0x000B7228; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61C8);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61CC);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        ctx->lr = 0x000B723C; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61C0);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61C4);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        ctx->lr = 0x000B7250; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61B8);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61BC);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        ctx->lr = 0x000B7264; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61B0);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61B4);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        ctx->lr = 0x000B7278; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61A8);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61AC);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        ctx->lr = 0x000B728C; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61A0);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61A4);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        ctx->lr = 0x000B72A0; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x619C);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6198);\n        ctx->lr = 0x000B72B0; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x6194);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6190);\n        ctx->lr = 0x000B72C0; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x618C);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6188);\n        ctx->lr = 0x000B72D0; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x6184);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6180);\n        ctx->lr = 0x000B72E0; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x617C);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6178);\n        ctx->lr = 0x000B72F0; func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B72F8; func_0012CA28(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7300; func_00100BC4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7308; func_00100AA8(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7310; func_000FA5A0(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7318; func_000F8294(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7320; func_000F8D58(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7328; func_000F7A24(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7330; func_000F7718(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7338; func_000FB3B0(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7340; func_000F9A00(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7348; func_000FAE60(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7350; func_000F9EF4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7358; func_000FAC18(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7360; func_000F97B8(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7368; func_000F92EC(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7370; func_00054360(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7378; func_0001A084(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7380; func_0002E0C4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7388; func_00041D5C(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[30] = vm_read32(ctx->gpr[2] + -0x6288);\n        ctx->gpr[0] = vm_read8(ctx->gpr[30] + 0x50);\n        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n        if ((!((ctx->cr >> 0) & 2))) goto loc_000B73D0;\n        ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x6298);\n        ctx->gpr[4] = (int64_t)(int32_t)(0);\n        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n        ctx->gpr[9] = ctx->gpr[9] + (int64_t)(0x408);\n        ctx->gpr[11] = ppc_rldicl(ctx->gpr[9], 0, 32);\n        ctx->gpr[11] = vm_read32(ctx->gpr[11] + 0x0);\n        { int64_t a = (int32_t)ctx->gpr[11]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n        if (((ctx->cr >> 0) & 2)) goto loc_000B73C0;\n        ctx->gpr[4] = ctx->gpr[11] + ctx->gpr[9];\nloc_000B73C0:\n        ctx->gpr[4] = ppc_rldicl(ctx->gpr[4], 0, 32);\n        ctx->gpr[3] = ctx->gpr[30] + (int64_t)(0x50);\n        ctx->lr = 0x000B73CC; func_003731A0(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\nloc_000B73D0:\n        ctx->lr = 0x000B73D4; func_000E4950(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[28] = (int64_t)(int32_t)(0);\n        ctx->lr = 0x000B73E0; func_0010F5E8(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B73E8; func_000C992C(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B73F0; func_002A6608(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B73F8; func_000CBC20(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7400; func_0013EEF8(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7408; func_0009782C(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7410; func_00058324(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7418; func_0002DB50(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7420; func_000C9EAC(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7428; func_000CE118(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7430; func_00112CC4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7438; func_000D3FAC(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7440; func_0010C024(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->lr = 0x000B7448; func_000B70B0(ctx); DRAIN_TRAMPOLINE(ctx);\n        ctx->gpr[3] = (int64_t)(int32_t)(0x2024);\n        ctx->lr = 0x000B7450; func_00263B70(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[29] = ctx->gpr[3] | ctx->gpr[3];\n        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        ctx->lr = 0x000B7460; func_000393E0(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x62A8);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6174);\n        ctx->gpr[0] = (int64_t)(int32_t)(4);\n        ctx->gpr[5] = (int64_t)(int32_t)(0x18);\n        ctx->gpr[3] = ctx->gpr[1] + (int64_t)(0x74);\n        vm_write32(ctx->gpr[9] + 0x0, ctx->gpr[29]);\n        ctx->gpr[29] = ctx->gpr[1] + (int64_t)(0x94);\n        vm_write32(ctx->gpr[1] + 0x70, ctx->gpr[0]);\n        ctx->lr = 0x000B7488; func_003735B4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[5] = vm_read32(ctx->gpr[2] + -0x6170);\n        ctx->gpr[29] = ppc_rldicl(ctx->gpr[29], 0, 32);\n        ctx->gpr[11] = (int64_t)(int32_t)(0);\n        ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];\n        ctx->gpr[0] = (int64_t)(int32_t)(0);\n        ctx->gpr[9] = (int64_t)(int32_t)(0x500);\n        ctx->gpr[4] = ctx->gpr[1] + (int64_t)(0x70);\n        vm_write8(ctx->gpr[1] + 0x8B, ctx->gpr[0]);\n        vm_write16(ctx->gpr[1] + 0x8E, ctx->gpr[9]);\n        vm_write16(ctx->gpr[1] + 0x92, ctx->gpr[11]);\n        vm_write16(ctx->gpr[1] + 0x8C, ctx->gpr[11]);\n        vm_write16(ctx->gpr[1] + 0x90, ctx->gpr[11]);\n        ctx->lr = 0x000B74C0; func_00251230(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x626C);\n        ctx->gpr[4] = ctx->gpr[29] | ctx->gpr[29];\n        ctx->gpr[11] = vm_read32(ctx->gpr[31] + 0x10);\n        ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x14);\n        ctx->gpr[0] = vm_read32(ctx->gpr[10] + 0x0);\n        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n        ctx->ctr = (uint32_t)ctx->gpr[0];\n        ctx->gpr[2] = vm_read32(ctx->gpr[10] + 0x4);\n        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n        ctx->gpr[29] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        vm_write32(ctx->gpr[29] + 0x1C, ctx->gpr[28]);\n        ctx->gpr[3] = vm_read32(ctx->gpr[1] + 0xD8);\n        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n        if (((ctx->cr >> 0) & 2)) goto loc_000B7514;\n        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        ctx->lr = 0x000B7510; func_00262FE4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\nloc_000B7514:\n        ctx->gpr[0] = vm_read16(ctx->gpr[29] + 0x2);\n        ctx->gpr[4] = ctx->gpr[29] | ctx->gpr[29];\n        ctx->gpr[0] = (uint64_t)ppc_rlwinm((uint32_t)ctx->gpr[0], 2, 14, 29);\n        ctx->gpr[11] = vm_read32((ctx->gpr[31] + ctx->gpr[0]));\n        ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n        ctx->gpr[10] = vm_read32(ctx->gpr[9] + 0x18);\n        ctx->gpr[0] = vm_read32(ctx->gpr[10] + 0x0);\n        vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n        ctx->ctr = (uint32_t)ctx->gpr[0];\n        ctx->gpr[2] = vm_read32(ctx->gpr[10] + 0x4);\n        ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n        ctx->gpr[2] = 0x00541178ULL; /*TOCFIX ld r2,N(r1)*/\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x616C);\n        vm_write32(ctx->gpr[9] + 0x0, ctx->gpr[28]);\n        ctx->lr = 0x000B7554; func_000B3950(ctx); DRAIN_TRAMPOLINE(ctx);\n        ctx->lr = 0x000B7558; func_000B5D10(ctx); DRAIN_TRAMPOLINE(ctx);\n        ctx->gpr[0] = (int64_t)(int32_t)(1);\n        ctx->gpr[3] = (int64_t)(int32_t)(0);\n        vm_write32(ctx->gpr[30] + 0x64, ctx->gpr[0]);\n        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x120);\n        ctx->gpr[28] = _cs_28;\n        ctx->gpr[29] = _cs_29;\n        ctx->lr = ctx->gpr[0];\n        ctx->gpr[30] = _cs_30;\n        ctx->gpr[31] = _cs_31;\n        ctx->gpr[1] = ctx->gpr[1] + (int64_t)(0x110);\n        return;\n}\n'

# Corpo COM o bloco, extraido do lift de producao menos as probes alheias.
PATCHED_BODY_B71 = 'void func_000B71B8(ppu_context* ctx) {\n        g_b71_product_reused = 0;\n        vm_write64(ctx->gpr[1] + -0x110, ctx->gpr[1]); ctx->gpr[1] += -0x110;\n        ctx->gpr[0] = ctx->lr;\n        vm_write64(ctx->gpr[1] + 0x100, ctx->gpr[30]);\n        vm_write64(ctx->gpr[1] + 0x120, ctx->gpr[0]);\n        vm_write64(ctx->gpr[1] + 0xF0, ctx->gpr[28]);\n        vm_write64(ctx->gpr[1] + 0xF8, ctx->gpr[29]);\n        vm_write64(ctx->gpr[1] + 0x108, ctx->gpr[31]);\n        func_002BA4B4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_002B6C40(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_002B7644(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_0004476C(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000CE0A0(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_00040090(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61D8);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61DC);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61D0);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61D4);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61C8);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61CC);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61C0);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61C4);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61B8);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61BC);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61B0);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61B4);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61A8);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61AC);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x61A0);\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x61A4);\n        ctx->gpr[4] = vm_read32(ctx->gpr[9] + 0x0);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x619C);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6198);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x6194);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6190);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x618C);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6188);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x6184);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6180);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[3] = vm_read32(ctx->gpr[2] + -0x617C);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6178);\n        func_002508E4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_0012CA28(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_00100BC4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_00100AA8(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000FA5A0(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000F8294(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000F8D58(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000F7A24(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000F7718(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000FB3B0(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000F9A00(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000FAE60(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000F9EF4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000FAC18(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000F97B8(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000F92EC(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_00054360(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_0001A084(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_0002E0C4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_00041D5C(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[30] = vm_read32(ctx->gpr[2] + -0x6288);\n        ctx->gpr[0] = vm_read8(ctx->gpr[30] + 0x50);\n        { int64_t a = (int32_t)ctx->gpr[0]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n        if ((!((ctx->cr >> 0) & 2))) goto loc_000B73D0;\n        ctx->gpr[11] = vm_read32(ctx->gpr[2] + -0x6298);\n        ctx->gpr[4] = (int64_t)(int32_t)(0);\n        ctx->gpr[9] = vm_read32(ctx->gpr[11] + 0x0);\n        ctx->gpr[9] = (int64_t)(int32_t)(ctx->gpr[9] + 0x408);\n        ctx->gpr[11] = ppc_rldicl(ctx->gpr[9], 0, 32);\n        ctx->gpr[11] = vm_read32(ctx->gpr[11] + 0x0);\n        { int64_t a = (int32_t)ctx->gpr[11]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n        if (((ctx->cr >> 0) & 2)) goto loc_000B73C0;\n        ctx->gpr[4] = ctx->gpr[11] + ctx->gpr[9];\nloc_000B73C0:\n        ctx->gpr[4] = ppc_rldicl(ctx->gpr[4], 0, 32);\n        ctx->gpr[3] = (int64_t)(int32_t)(ctx->gpr[30] + 0x50);\n        func_003731A0(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\nloc_000B73D0:\n        func_000E4950(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[28] = (int64_t)(int32_t)(0);\n        func_0010F5E8(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000C992C(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_002A6608(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000CBC20(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_0013EEF8(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_0009782C(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_00058324(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_0002DB50(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000C9EAC(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000CE118(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_00112CC4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000D3FAC(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_0010C024(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        func_000B70B0(ctx); DRAIN_TRAMPOLINE(ctx);\n        { static int _n=0; if(_n++<4) fprintf(stderr,"[POSTINTRO] B71 after B70B0\\n"); }\n        ctx->gpr[3] = (int64_t)(int32_t)(0x2024);\n        func_00263B70(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        { static int _n=0; if(_n++<4)\n            fprintf(stderr,"[POSTINTRO] B71 after alloc0x2024 r3=0x%08X\\n",\n              (unsigned)(uint32_t)ctx->gpr[3]); }\n        ctx->gpr[29] = ctx->gpr[3] | ctx->gpr[3];\n        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        /* 393E0: ~3k-line registry fill (hundreds of 2F4FC). In-boot hangs\n         * inside it (never returns) — TOC/string/table state incomplete after\n         * partial WAD. Stamp vtable only so B71 can set +0x64 and exit; full\n         * registry is a follow-up (Wall D / type factory). Opt-in full:\n         * PS3_B71_FULL_393E0=1 */\n        if ((uint32_t)ctx->gpr[29] != 0u) {\n            const char* _full = getenv("PS3_B71_FULL_393E0");\n            if (_full && *_full && *_full != \'0\') {\n                func_000393E0(ctx); DRAIN_TRAMPOLINE(ctx);\n            } else {\n                uint32_t _obj = (uint32_t)ctx->gpr[29];\n                uint32_t _vt = vm_read32((uint32_t)ctx->gpr[2] - 0x7A6Cu);\n                vm_write32(_obj + 0x0, _vt);\n                vm_write32(_obj + 0x1FD4u, 0u);\n                vm_write32(_obj + 0x1FCCu, 0u);\n                vm_write32(_obj + 0x1FD0u, 0u);\n                { static int _n=0; if(_n++<4)\n                    fprintf(stderr,"[POSTINTRO] B71 393E0 HLE-lite obj=0x%08X vt=0x%08X "\n                      "(set PS3_B71_FULL_393E0=1 for full)\\n", _obj, _vt); }\n            }\n        } else {\n            { static int _n=0; if(_n++<4)\n                fprintf(stderr,"[POSTINTRO] B71 skip 393E0 (null alloc)\\n"); }\n        }\n        /* nop */;\n        { static int _n=0; if(_n++<4) fprintf(stderr,"[POSTINTRO] B71 after 393E0\\n"); }\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x62A8);\n        ctx->gpr[4] = vm_read32(ctx->gpr[2] + -0x6174);\n        ctx->gpr[0] = (int64_t)(int32_t)(4);\n        ctx->gpr[5] = (int64_t)(int32_t)(0x18);\n        ctx->gpr[3] = (int64_t)(int32_t)(ctx->gpr[1] + 0x74);\n        if ((uint32_t)ctx->gpr[9] >= 0x10000u)\n            vm_write32(ctx->gpr[9] + 0x0, ctx->gpr[29]);\n        ctx->gpr[29] = (int64_t)(int32_t)(ctx->gpr[1] + 0x94);\n        vm_write32(ctx->gpr[1] + 0x70, ctx->gpr[0]);\n        func_003735B4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        ctx->gpr[5] = vm_read32(ctx->gpr[2] + -0x6170);\n        ctx->gpr[29] = ppc_rldicl(ctx->gpr[29], 0, 32);\n        ctx->gpr[11] = (int64_t)(int32_t)(0);\n        ctx->gpr[3] = ctx->gpr[29] | ctx->gpr[29];\n        ctx->gpr[0] = (int64_t)(int32_t)(0);\n        ctx->gpr[9] = (int64_t)(int32_t)(0x500);\n        ctx->gpr[4] = (int64_t)(int32_t)(ctx->gpr[1] + 0x70);\n        vm_write8(ctx->gpr[1] + 0x8B, ctx->gpr[0]);\n        vm_write16(ctx->gpr[1] + 0x8E, ctx->gpr[9]);\n        vm_write16(ctx->gpr[1] + 0x92, ctx->gpr[11]);\n        vm_write16(ctx->gpr[1] + 0x8C, ctx->gpr[11]);\n        vm_write16(ctx->gpr[1] + 0x90, ctx->gpr[11]);\n        func_00251230(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\n        { static int _n=0; if(_n++<4) fprintf(stderr,"[POSTINTRO] B71 after 251230\\n"); }\n        ctx->gpr[31] = vm_read32(ctx->gpr[2] + -0x626C);\n        ctx->gpr[4] = ctx->gpr[29] | ctx->gpr[29];\n        ctx->gpr[11] = ((uint32_t)ctx->gpr[31] >= 0x10000u) ? vm_read32(ctx->gpr[31] + 0x10) : 0;\n        ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n        /* Guard factory icall: bad vt → skip (same class as CB56C). */\n        { uint32_t _ent=(uint32_t)ctx->gpr[11];\n          if (_ent) {\n            ps3_factory_repair_vt(_ent);\n            /* type 4 factory (0x400D6808 family) freelist often empty after first\n             * WAD product — replenish before construct. */\n            ps3_factory_freelist_replenish(_ent, 0x0004u);\n          }\n          uint32_t _vt = (_ent >= 0x10000u && _ent < 0x4F000000u) ? vm_read32(_ent) : 0u;\n          uint32_t _opd = (_vt >= 0x10000u && _vt < 0x4F000000u) ? vm_read32(_vt + 0x14u) : 0u;\n          uint32_t _code = (_opd >= 0x10000u && _opd < 0x4F000000u) ? vm_read32(_opd) : 0u;\n          uint32_t _toc = (_opd >= 0x10000u && _opd < 0x4F000000u) ? vm_read32(_opd + 4u) : 0u;\n          { static int _n=0; if(_n++<8)\n              fprintf(stderr,"[POSTINTRO] B71 icallA ent=0x%08X vt=0x%08X code=0x%08X\\n",\n                _ent, _vt, _code); }\n          vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n          if (_ent && _vt && _opd && _code >= 0x10000u && _code < 0x01000000u) {\n            ctx->gpr[9] = _vt; ctx->gpr[10] = _opd; ctx->gpr[0] = _code;\n            ctx->ctr = _code; ctx->gpr[2] = _toc;\n            ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n          } else {\n            ctx->gpr[3] = 0;\n            { static int _n=0; if(_n++<8) fprintf(stderr,"[POSTINTRO] B71 icallA SKIP\\n"); }\n          }\n          /* If construct returned null, reuse existing product from fo+0x48. */\n          g_b71_product_reused = 0;\n          if ((uint32_t)ctx->gpr[3] == 0u && _ent) {\n            uint32_t rp = ps3_factory_reuse_product(_ent);\n            if (rp) {\n              ctx->gpr[3] = rp;\n              g_b71_product_reused = 1;\n              { static int _n=0; if(_n++<8)\n                  fprintf(stderr,"[POSTINTRO] B71 icallA reuse product r3=0x%08X\\n", rp); }\n            }\n          }\n          { static int _n=0; if(_n++<8)\n              fprintf(stderr,"[POSTINTRO] B71 icallA product r3=0x%08X reused=%d\\n",\n                (unsigned)(uint32_t)ctx->gpr[3], g_b71_product_reused); }\n          ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);\n        }\n        ctx->gpr[29] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        if ((uint32_t)ctx->gpr[29] >= 0x10000u)\n            vm_write32(ctx->gpr[29] + 0x1C, ctx->gpr[28]);\n        ctx->gpr[3] = vm_read32(ctx->gpr[1] + 0xD8);\n        { int64_t a = (int32_t)ctx->gpr[3]; int64_t b = (int64_t)0; uint32_t cr_val = (a < b) ? 8 : (a > b) ? 4 : 2; ctx->cr = (ctx->cr & ~(0xFu << 0)) | (cr_val << 0); }\n        if (((ctx->cr >> 0) & 2)) goto loc_000B7514;\n        ctx->gpr[3] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        func_00262FE4(ctx); DRAIN_TRAMPOLINE(ctx);\n        /* nop */;\nloc_000B7514:\n        if ((uint32_t)ctx->gpr[29] >= 0x10000u && (uint32_t)ctx->gpr[29] < 0x4F000000u) {\n            /* Skip attach when product was reuse-salvaged (already live). */\n            if (g_b71_product_reused) {\n              { static int _n=0; if(_n++<8)\n                  fprintf(stderr,"[POSTINTRO] B71 skip icallB (reuse product=0x%08X)\\n",\n                    (unsigned)(uint32_t)ctx->gpr[29]); }\n            } else {\n            ctx->gpr[0] = vm_read16(ctx->gpr[29] + 0x2);\n            ctx->gpr[4] = ctx->gpr[29] | ctx->gpr[29];\n            ctx->gpr[0] = (uint32_t)ppc_rlwinm((uint32_t)ctx->gpr[0], 2, 14, 29);\n            ctx->gpr[11] = ((uint32_t)ctx->gpr[31] >= 0x10000u)\n                ? vm_read32((ctx->gpr[31] + ctx->gpr[0])) : 0;\n            ctx->gpr[3] = ctx->gpr[11] | ctx->gpr[11];\n            { uint32_t _ent=(uint32_t)ctx->gpr[11];\n              uint32_t _vt = (_ent >= 0x10000u && _ent < 0x4F000000u) ? vm_read32(_ent) : 0u;\n              uint32_t _opd = (_vt >= 0x10000u && _vt < 0x4F000000u) ? vm_read32(_vt + 0x18u) : 0u;\n              uint32_t _code = (_opd >= 0x10000u && _opd < 0x4F000000u) ? vm_read32(_opd) : 0u;\n              uint32_t _toc = (_opd >= 0x10000u && _opd < 0x4F000000u) ? vm_read32(_opd + 4u) : 0u;\n              { static int _n=0; if(_n++<8)\n                  fprintf(stderr,"[POSTINTRO] B71 icallB ent=0x%08X code=0x%08X\\n", _ent, _code); }\n              vm_write64(ctx->gpr[1] + 0x28, ctx->gpr[2]);\n              if (_ent && _vt && _opd && _code >= 0x10000u && _code < 0x01000000u) {\n                ctx->ctr = _code; ctx->gpr[2] = _toc;\n                ps3_indirect_call(ctx); DRAIN_TRAMPOLINE(ctx);\n              } else {\n                { static int _n=0; if(_n++<8) fprintf(stderr,"[POSTINTRO] B71 icallB SKIP\\n"); }\n              }\n              ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);\n            }\n            }\n        } else {\n            { static int _n=0; if(_n++<8)\n                fprintf(stderr,"[POSTINTRO] B71 skip icallB (null product)\\n"); }\n        }\n        ctx->gpr[9] = vm_read32(ctx->gpr[2] + -0x616C);\n        if ((uint32_t)ctx->gpr[9] >= 0x10000u)\n            vm_write32(ctx->gpr[9] + 0x0, ctx->gpr[28]);\n        func_000B3950(ctx); DRAIN_TRAMPOLINE(ctx);\n        func_000B5D10(ctx); DRAIN_TRAMPOLINE(ctx);\n        ctx->gpr[0] = (int64_t)(int32_t)(1);\n        ctx->gpr[3] = (int64_t)(int32_t)(0);\n        vm_write32(ctx->gpr[30] + 0x64, ctx->gpr[0]);\n        /* POSTINTRO: boot sequence 000B71B8 completed (flag obj+0x64=1). */\n        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n            const char* e=getenv("PS3_TRACE_INTROSEQ");\n            const char* e2=getenv("PS3_TRACE_POSTINTRO");\n            _on=((e&&*e&&*e!=\'0\')||(e2&&*e2&&*e2!=\'0\'))?1:0;}\n          if(_on){ static int _n=0; if(_n++<3){\n            fprintf(stderr,"[POSTINTRO] exit func_000B71B8 #%d flag_obj=0x%08X +0x64=1\\n",\n                    _n, (unsigned)(uint32_t)ctx->gpr[30]);\n            fflush(stderr);} } }\n        ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0x120);\n        ctx->gpr[28] = vm_read64(ctx->gpr[1] + 0xF0);\n        ctx->gpr[29] = vm_read64(ctx->gpr[1] + 0xF8);\n        ctx->lr = ctx->gpr[0];\n        ctx->gpr[30] = vm_read64(ctx->gpr[1] + 0x100);\n        ctx->gpr[31] = vm_read64(ctx->gpr[1] + 0x108);\n        ctx->gpr[1] = (int64_t)(int32_t)(ctx->gpr[1] + 0x110);\n        return;\n}\n'

SIG_CB = "void func_000CB56C(ppu_context* ctx) {\n"
MARKER_CB = "PS3_TYPE15_CB56C"
ANCHOR_CB = '        ctx->gpr[29] = ppc_rldicl(ctx->gpr[3], 0, 32);\n        ctx->gpr[28] = ctx->gpr[3] | ctx->gpr[3];\n'
BLOCK_CB = '        /* TYPE15: free-list often empty after first WAD construct; pin +0x48\n         * holds the live product list. Prefer that over pin-zone freelist shells\n         * (shell vt is often non-live 0x0020xxxx). Skip icall2 attach of live\n         * products. Opt-out: PS3_TYPE15_CB56C=0. */\n        int ty15_reused = 0;\n        { static int _on=-1; if(_on<0){extern char* getenv(const char*);\n            const char* e=getenv("PS3_TYPE15_CB56C");\n            /* Default ON. Opt-out: =0. */\n            if (!e) _on = 1;\n            else _on = (*e && *e!=\'0\') ? 1 : 0;}\n          uint32_t _prod_now = (uint32_t)ctx->gpr[3];\n          int shell_bad = (_prod_now >= 0x47D00800u && _prod_now < 0x47D00C00u);\n          if (_on && ((uint32_t)ctx->gpr[3] == 0u || shell_bad)) {\n            uint32_t _ent = 0x47D00000u;\n            uint32_t _vt = vm_read32(_ent);\n            if (_vt == 0x00516D70u) {\n              uint32_t _hdr = vm_read32(_ent + 0x48u);\n              if (_hdr >= 0x10000u && _hdr < 0x4F000000u) {\n                uint32_t _prod = _hdr + 4u;\n                if (_prod >= 0x10000u && _prod < 0x4F000000u) {\n                  ctx->gpr[3] = _prod;\n                  ty15_reused = 1;\n                  { static int _n=0; if(_n++<8)\n                      fprintf(stderr,"[TYPE15] CB56C reuse product hdr=0x%08X prod=0x%08X "\n                        "(skip icall2; was_shell=%d)\\n",\n                        _hdr, _prod, shell_bad); }\n                }\n              }\n            }\n          }\n        }\n'

# Simbolos host de que o bloco do B71 depende (injeccoes de preambulo).
# (nome, agulha que prova que existe declaracao/definicao no lift)
DEPS = (
    ("ps3_factory_repair_vt", 'int ps3_factory_repair_vt(uint32_t'),
    ("ps3_factory_reuse_product", 'uint32_t ps3_factory_reuse_product(uint32_t'),
    ("ps3_factory_freelist_replenish", 'int ps3_factory_freelist_replenish(uint32_t'),
)


def region(text: str, sig: str):
    """(inicio, fim) do corpo da funcao `sig`, ou None se nao estiver aqui."""
    i = text.find(sig)
    if i < 0:
        return None
    j = text.find("\nvoid func_", i + len(sig))
    return (i, len(text) if j < 0 else j)


def write(path: Path, text: str) -> None:
    try:
        path.write_text(text, encoding="utf-8", newline="\n")
    except TypeError:                                  # Python < 3.10
        path.write_text(text, encoding="utf-8")


def do_b71(path: Path) -> int:
    """0 aplicado | 1 ALREADY | -1 nao tem a funcao | -2 corpo inesperado."""
    t = path.read_text(encoding="utf-8", errors="replace")
    if SIG_B71 not in t:
        return -1
    if MARKER_B71 in t:
        print(f"  {path.name}: func_000B71B8 ALREADY")
        return 1
    if CLEAN_BODY_B71 not in t:
        return -2
    t = t.replace(CLEAN_BODY_B71, PATCHED_BODY_B71, 1)
    if "g_b71_product_reused = 0;\n\n" + SIG_B71 not in t:
        t = t.replace(SIG_B71, DECL_B71 + SIG_B71, 1)
    write(path, t)
    print(f"  {path.name}: func_000B71B8 APPLIED "
          f"({PATCHED_BODY_B71.count(chr(10))} linhas)")
    return 0


def do_cb56c(path: Path) -> int:
    """0 aplicado | 1 ALREADY | -1 nao tem a funcao | -2 ancora inesperada."""
    t = path.read_text(encoding="utf-8", errors="replace")
    span = region(t, SIG_CB)
    if span is None:
        return -1
    b0, b1 = span
    if MARKER_CB in t[b0:b1]:
        print(f"  {path.name}: func_000CB56C ALREADY")
        return 1
    if t.count(ANCHOR_CB, b0, b1) != 1:
        return -2
    at = t.index(ANCHOR_CB, b0, b1)
    write(path, t[:at] + BLOCK_CB + t[at:])
    print(f"  {path.name}: func_000CB56C APPLIED ({BLOCK_CB.count(chr(10))} linhas)")
    return 0


def main() -> int:
    paths = [p for p in resolve_lift_paths(sys.argv[1:], "recomp_macos_v2")
             if p.is_file()]
    if not paths:
        print("ERRO: nenhum ficheiro de lift legivel", file=sys.stderr)
        return 3

    rc = 0
    host = None                      # chunk que ficou com o bloco do B71
    for label, fn in (("func_000B71B8", do_b71), ("func_000CB56C", do_cb56c)):
        hits = []
        for p in paths:
            r = fn(p)
            hits.append(r)
            if label == "func_000B71B8" and r in (0, 1):
                host = p
        if any(r == -2 for r in hits):
            print(f"ERRO: {label} existe mas o corpo/ancora gerado NAO e' o "
                  f"esperado.\n"
                  "  O lifter mudou de forma. Nao substituo as cegas: revalida o\n"
                  "  bloco contra um lift de producao conhecido-bom e regenera\n"
                  "  este script.", file=sys.stderr)
            rc = 2
        elif not any(r in (0, 1) for r in hits):
            print(f"ERRO: {label} nao existe em nenhum dos ficheiros dados.",
                  file=sys.stderr)
            rc = max(rc, 1)

    if rc == 0 and host is not None:
        # A verificacao e' POR CHUNK: as chamadas ficam no chunk do B71, logo e'
        # ai que tem de existir prototipo (ou definicao) visivel. Uma definicao
        # noutro chunk nao chega para compilar este.
        t = host.read_text(encoding="utf-8", errors="replace")
        missing = [name for name, needle in DEPS if needle not in t]
        if missing:
            print(f"AVISO: {host.name} chama mas nao declara -> " + ", ".join(missing))
            print("  (injeccao de preambulo com dono proprio; fonte versionada em "
                  "../lift_baseline/injected_000.cpp e ../host_gow2_factory.cpp)")
    if rc == 0:
        print("[b71-cb56c-reuse] ok")
    return rc


if __name__ == "__main__":
    sys.exit(main())
