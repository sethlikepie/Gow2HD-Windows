# R6 — Real issuer de `cellGcmSetFlipCommand` / `_cellGcmSetFlipCommand` (2026-07-23)

**Tipo:** diagnóstico (HLE path audit + host BT Darwin) — **sem** fix de re-arm.  
**Gate B:** GREEN · **Gate A:** RED (`SetFlip_after_R_Perm=0`)  
**Log canónico:** `/tmp/r6_mainloop.log`  
**Recipe:**
```bash
TIMEOUT=90 LOG=/tmp/r6_mainloop.log PS3_NO_RSX=1 PS3_TRACE_MAINLOOP=1 ./rodar_gow2_menu_fast.sh
python3 count_menu_gate.py /tmp/r6_mainloop.log
```

Prior: R5 refutou `func_00017ACC` (0 enters) e reclassificou MAINLOOP `g={17ADE,17ADF}` como stack residual.

---

## 1. HLE path audit (`g_last_hle_caller_*`)

| Item | Achado |
|------|--------|
| Onde é set | `runtime/ppu/ppu_hle.cpp` → `ps3_hle_call` |
| Mac TLS | `__thread` + `extern "C"` (correcto; C++ `thread_local` partia TLV no Apple) |
| `g_last_hle_caller_lr` | **sempre 0** nestes flips (tail/trampoline import — esperado) |
| `g_last_hle_caller_guest[]` | scan heurístico do guest SP; **residual** `{17ADE,17ADF[,4CBC18]}` — **não** é return-address live (R5) |
| Correctness Mac | **Sim** — valores são escritos; **não** identificam o issuer real |
| Novo | `g_last_hle_nid` stampado no entry de `ps3_hle_call` (todas as rotas) |

Conclusão: o watermark guest era **correcto mas insuficiente**. R6 adiciona **host backtrace + `dladdr`** (Darwin) e **NID** no MAINLOOP.

---

## 2. Probe shipped (ps3recomp, default OFF)

| Artefacto | Função |
|-----------|--------|
| `runtime/ppu/ppu_hle.cpp` | `g_last_hle_nid`; stamp LR/NID no entry; SCAN_LIMIT Darwin 0x40→0x200 |
| `libs/video/cellGcmSys.c` | `PS3_TRACE_MAINLOOP=1`: NID + host_bt (1–12 + %500) + `dladdr` → `func_*`; hist `uniq_host` / `uniq_nid` no NOTE WAD |

Sem default ON. Sem flip HLE forjado.

---

## 3. Smoke metrics

| Métrica | Valor |
|---------|------:|
| SetFlip_total | 23033 |
| SetFlip_after_R_Perm | **0** |
| R_Perm full | 1 |
| thr_end | 1 |
| ICALL_BAD_after | 12 |
| Gate B | **GREEN** |
| Gate A | **RED** |

---

## 4. Real issuer (in-boot, host BT)

### NID

| Campo | Valor |
|-------|-------|
| **NID único** | **`0x21397818`** |
| Nome HLE | **`_cellGcmSetFlipCommand`** (wrapper → `cellGcmSetFlipCommand`) |
| **Não** é | `cellGcmSetFlipCommand` público `0x5C770579` |
| Contagem | `uniq_nid n=1: 0x21397818×22917` (no open Lgl) |

### Host leaf (guest recomp)

| Fase | issuer | samples (BT) |
|------|--------|-------------:|
| cedo (até ~#2000, logo movie) | **`func_002EFD60`** | 16 |
| tarde (até open Lgl) | **`func_002EFDA4`** | 41 |
| **post R_Perm** | *(nenhum SetFlip)* | 0 |

`func_002EFDA4` é **fragmento** do mesmo helper GCM: path grow/icall `2EFDD4` → continua em `2EFDA4` → `func_004B9818` (import trampoline) → HLE.

### Cadeia host estável (todos os samples)

```
main → ppu_run
  → func_00010230 → func_00010354
  → func_0025C838 → func_002B2E74      # mainloop / B71 parent (hot)
  → func_000B71B8 → func_000CE0A0 → func_000CDD88
  → func_00156680 → func_0014FE18      # chama 2EFD60 com buffer id
  → func_002EFD60 | func_002EFDA4
  → func_004B9818                      # import stub (slot ~0x51935C)
  → ps3_import_thunk → ps3_hle_call(0x21397818)
  → _cellGcmSetFlipCommand → cellGcmSetFlipCommand
```

**`func_00017ACC` NÃO aparece** em nenhum host_bt — confirma R5.

### Watermark WAD

```
NOTE /wad/r_lglsca.wad_ps3 last_flip=#22917 nid=0x21397818 lr=0x0 host=func_002EFDA4 g={17ADE,17ADF,0,0}
NOTE /wad/r_perma.wad_ps3  last_flip=#22917 (0 flips durante stream)
uniq_host: func_002EFD60×16  func_002EFDA4×41
```

---

## 5. Classificação

| Classe | Veredito |
|--------|----------|
| Issuer de flip intro | Path **GCM present helper** `func_002EFD60` (+frag `2EFDA4`), via parent `14FE18` ← … ← `2B2E74` (mainloop B71) |
| Porquê pára no WAD | Mesma root class R3: modo **intro-present → asset-load** no open `R_LglScA` **deixa de chamar** este path; **sem re-arm** pós thr |
| Sticky/HLE nosso a matar flip? | **Não evidenciado** — nenhum patch em `2EFD60`/`14FE18`; HLE `_cellGcmSetFlipCommand` só responde a calls guest; guest **para de chamar** |
| Gate A fix nesta task? | **Não** — precisa re-agendar o chain `14FE18`/`2EFD60` (ou path menu equivalente) pós thr |

---

## 6. Próximo gate (R7)

**Instrumentar enters** de `func_002EFD60` / `func_0014FE18` (e opcional `func_00156680`) **pre vs post R_Perm** (`g_ps3_rperma_full`), gated `PS3_TRACE_2EFD60=1`.

Perguntas discriminadoras:
1. `14FE18` / `2EFD60` tot≫0 pre e post=0? (path morto pós load — quem deveria re-chamar?)
2. Entra em `2EFD60` pós thr mas falha branches (buffer id ≥7 / grow fail → `2EFDF8` erro `0x802100FF`) sem chegar ao import?
3. Parent `2B2E74` continua hot (TYPE15/CC9D0) mas **pula** o ramo que chega a `156680`/`14FE18`?

**Não** reabrir 17ACC re-arm. **Não** forjar flip HLE.

---

## 7. Commits

- ps3recomp: `diag(ppu/gcm): R6 host BT + NID real flip issuer (MAINLOOP)`
- gow2-recomp: `diag(gow2): R6 real SetFlip issuer 2EFD60/_cellGcmSetFlipCommand`
