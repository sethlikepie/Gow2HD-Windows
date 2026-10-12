# R2 — Disc ICALL-BAD vs SetFlip_after_R_Perm (2026-07-23)

**Tipo:** discriminadores H3 (causalidade flip) + H1 (evidência mem).  
**Gate A continua RED.** **Gate B GREEN** em ambos os runs.  
**Não claim:** menu / SetFlip_after_R_Perm≥1 / fix de present.

**Código:** `ps3recomp/runtime/ppu/ppu_loader.cpp` (`ps3_indirect_call`)
- `PS3_ICALL_HEAP40=1` (default **OFF**): skip ctr ∈ `[0x40000000, 0x4F000000)` como ICALL-HEAP (r3=0, sem streak FATAL).
- `PS3_TRACE_ICALL_BAD_MEM=1` (default **OFF**): dump 16 words em r3, r12, ctr−0x10 no 1º ICALL-BAD.

---

## Disc A — H3 (silenciar 0x40 heap-as-code restaura flip?)

| Run | Env | SetFlip_after_R_Perm | Gate B | ICALL-BAD | Notas |
|-----|-----|---------------------:|--------|----------:|-------|
| **base** | `PS3_NO_RSX=1` + `PS3_TRACE_ICALL_BAD_MEM=1` | **0** | GREEN | 15 | ctr `0x40637488` ×12 + residual |
| **disc** | `PS3_NO_RSX=1` `PS3_ICALL_HEAP40=1` | **0** | GREEN | 12 | `0x40637488` → `[ICALL-HEAP40] skip` ×16; ICALL-BAD vira lixo OOB (`0xFB788082`…) |

Logs: `/tmp/r2_base.log`, `/tmp/r2_heap40.log` (TIMEOUT=90, `./rodar_gow2_menu_fast.sh`).

**Veredito H3: DEAD.** Silenciar a banda 0x40 freelist **não** restaura `SetFlip_after_R_Perm`.  
Coerente com R1: flip morre no open `R_LglScA` **antes** do 1º ICALL-BAD.  
HEAP40 **não** vira LIVE_SKIP permanente — fica opt-in diagnóstico only.

---

## Disc B — H1 evidência (dump 1º ICALL-BAD)

Primeiro hit (base, estável vs R1):

```
[ICALL-BAD] ctr=0x40637488 lr=0 r2=0x40637478 r3=0x400C3D88 r11=0x400C3D88 r12=0x40004024
[ICALL-BAD-MEM] r3 base=0x400C3D88 words16:
  9102FFFF 07009502 B90B9002 FFFF0800 9502BA0B 8F02FFFF 09009502 BB0B8E02
  FFFF0A00 9502BC0B 1F00FFFF 0B009502 BB0B8E02 FFFF0C00 9502BA0B 8F02FFFF
[ICALL-BAD-MEM] r12 base=0x40004024 words16:
  40637AC8 400FB744 00000000 40004020 4000404C 4300401C 526F6F74 00000000
  00000000 00000000 80000001 8400000D 8400000D 00000000 00000000 00000000
[ICALL-BAD-MEM] ctr-0x10 base=0x40637478 words16:
  4063686C 00000000 80000004 80000004 40637498 40004020 80000004 80000004
  40637498 40637498 80000004 80000189 27182818 406374C0 00000000 40004020
```

| Base | Leitura |
|------|---------|
| **r3=`0x400C3D88`** | 1ª word `0x9102FFFF` = **tag freelist 0x91…** (não vtable/OPD). `this` corrompido / reutilizado como freelist. |
| **r12=`0x40004024`** | arena+4: head `0x40637AC8`, back-ref `0x40004020`, ASCII `"Root"`. Controlo do alocador, não OPD de objeto. |
| **ctr−0x10=`0x40637478`** | nó freelist OPD-shaped; embute **`0x27182818`** (ICALL-HEAP já guardado) + arena `0x40004020`. Confirma chain freelist→icall. |

**H1:** reforçada (evidência in-boot; **sem fix**). O icall não é método legítimo do obj — freelist lido como código/OPD.  
Writer exacto do slot em `0x400C3D88` / path de carga do CTR **ainda aberto** (R3).

---

## Aceite R2

| Critério | Status |
|----------|--------|
| `PS3_ICALL_HEAP40` gated default OFF | **PASS** |
| Smoke base + HEAP40; count_menu_gate | **PASS** |
| H3 LIVE/DEAD medido | **PASS — DEAD** |
| SetFlip_after ambos | **0 / 0** (Gate A RED; sem claim verde) |
| Gate B baseline | **GREEN** |
| Dump H1 gated | **PASS** (3 linhas MEM) |
| Sem LIVE_SKIP permanente | **PASS** |

---

## Próximo (R3)

1. **H1 writer:** quem grava `*(obj…)` / OPD que vira ctr=`0x40637488` (ou carrega arena+4 em r12 no path de icall). Probe de store em `0x400C3D88` / freelist walk no pump R_Perm — **não** silenciar 0x40 como “fix”.
2. **Causa do stop de flip em open Lgl** (pré-ICALL): timeline R1 — reentrada present/UI/movie FSM no open WAD, independente do ICALL-BAD.
3. H2 stream/typemap se writer for parse F2B.

**Prior:** `notes/2026-07-23-r1-icall-bad-present.md` (`1a540c8`).  
**Plano:** `ps3recomp/docs/superpowers/plans/2026-07-23-m3plus-postwad-present.md` Task R2.
