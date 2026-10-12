# Veredito Gate A — Track M (M0–M5) + M3+ residual (R1–R12) — 2026-07-23

**Fecho honesto:** Gate **B GREEN**, Gate **A RED**. Menu UI **NO**. Playable **NO**.

Track M (TYPE15 / CC9D0 / frame loop pós-B71) **fecha o diagnóstico** com
medição e um fix estrutural (M2 CLOSE-PRESERVE). **Não** fecha o wall de menu:
o guest ainda não reentra em present/pad após open de WAD.

**M3+ residual (R1–R12)** afiou a root class: ICALL-BAD freelist-as-code **não**
é a causa do corte de flip; HEAP40 skip H3 **DEAD**; issuer reclass=`2EFD60`;
main pós thr preso no triângulo **`2B2DD0→B951C→CC9D0` (f4=0)** — **sem re-arm**
(TIMEOUT=90/120 `SetFlip_after_R_Perm=0`). Wall activo = **TYPE15 f4 / product
work incompleto** dentro do parent tick (present re-arm dependente).

Índices:
- `ps3recomp/docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md`
- `ps3recomp/docs/superpowers/plans/2026-07-20-00-wall-chain-INDEX.md`
- Plano Track M: `ps3recomp/docs/superpowers/plans/2026-07-23-menu-type15-frame-loop.md`
- Plano residual: `ps3recomp/docs/superpowers/plans/2026-07-23-m3plus-postwad-present.md`

Recipe canónico: `./rodar_gow2_menu_fast.sh` (ou env equivalente) +
`python3 count_menu_gate.py <log>`; kill por PID; `PS3_NO_RSX=1` suficiente
para métricas de SetFlip/pad (printf guest).

---

## Tabela final de gates (medida M0–M5 + M3+ R1–R3)

| Gate | Status | Evidence |
|------|--------|----------|
| **B** | **GREEN** | thr_end≥1, R_Perm full **20169344**, StartSeq real (≥2 async path / FORCE+REPLAY-NOPIC), **PARK=0**, **FATAL=0**; smokes M0–M4 + R1–R3 |
| **A** | **RED** | **SetFlip_after_R_Perm=0** (incl. TIMEOUT=120), **Pad_after_R_Perm=0** após linha R_Perm full / B71 |
| **Menu UI** | **NO** | sem FSM de menu / NewGame / Press Start pós thr; st620 só `0→0` com `overlay_done=1` |
| **Playable** | **NO** | explícito — não claim |

### Contagens representativas (pós-M2 product list; M3 baseline NO_RSX)

| Métrica | Valor |
|---------|-------|
| thr_end | 1 |
| R_Perm full 20169344 | 1 |
| PARK / FATAL | 0 / 0 |
| CLOSE-PRESERVE / attach=full | 2 / 2 |
| SetFlip_total (intro pré-WAD) | milhares (ex. 3815–26k) |
| **SetFlip_after_R_Perm** | **0** |
| **Pad_after_R_Perm** (`cellPadGetData`) | **0** |
| Pad_total | **0** (guest nunca chama GetData neste boot) |
| ICALL-BAD after R_Perm | **12×** `ctr=0x40637488` |
| CC9D0 live this | só **2** EAs (`0x4066D798`, `0x4066D804`), f4=f54=**0** |

Timeline crítica: último `SetFlipCommand` **antes** de `movieio open` de
`R_LglScA` / stream `R_PermA` — zero flip/pad até timeout após R_Perm full.

---

## Progresso shipped (commits gow2-recomp)

| Task | SHA | Mensagem / outcome |
|------|-----|--------------------|
| **M0** | `5920aff` | `docs(gow2): baseline M0 menu-fast Gate B green / A red` — freeze counts + md5 |
| **M1** | `967525e` | `diag(gow2): discriminador CC9D0/TYPE15 pós-attach (M1)` — H1 primary (+ H3) |
| **M1 I1** | `b87e6a3` | `fix(gow2): alinhar env_on C/Python do disc TYPE15 (M1 I1)` |
| **M2** | `1f5d61c` | `fix(gow2): TYPE15 product/list preserve pós-sanitize (M2 — H1)` — CLOSE-PRESERVE nodes=12; f4 still 0 |
| **M3** | `90829b6` | `diag(gow2): M3 post-B71 flip still blocked — guest abandona present pós-WAD` — metrics + root class |
| **M4** | `e7cf689` | `diag(gow2): M4 pad/menu still blocked pós-WAD` — pad never polled post-WAD |
| **M5** | `124d697` | `docs(gow2): veredito Gate A RED Track M (M0–M4)` — fecho honesto |

Notas por task (mesmo repo):

| Task | Nota |
|------|------|
| M0 | `notes/2026-07-23-m0-baseline-counts.md` |
| M1 | `notes/2026-07-23-m1-cc9d0-discriminator.md` |
| M2 | `notes/2026-07-23-m2-product-fix.md` |
| M3 | `notes/2026-07-23-m3-postb71-flip.md` |
| M4 | `notes/2026-07-23-m4-pad-menu.md` |
| M5 | `notes/2026-07-23-gate-a-verdict.md` (este ficheiro) |
| R1 | `notes/2026-07-23-r1-icall-bad-present.md` |
| R2 | `notes/2026-07-23-r2-icall-flip-disc.md` |
| R3 | `notes/2026-07-23-r3-present-reentry.md` |
| R4 | `notes/2026-07-23-gate-a-verdict.md` (secção M3+ abaixo) |

### Resumo de outcomes

| Task | Resultado honesto |
|------|-------------------|
| M0 | Baseline: B green / A red |
| M1 | Survivor **H1** (lista esvaziada pelo RESET over-aggressive); H3 co-sinal (só 2 this) |
| M2 | **PASS estrutural** CLOSE-PRESERVE (`child_head` real, nodes=12); **f4 residual 0**; flip ainda 0 |
| M3 | Infra `count_menu_gate.py`; YIELD/LIVE_SKIP **não** desbloqueiam flip → root class: guest abandona present no open WAD |
| M4 | Pad Init cedo; **zero** GetData/GetInfo2; autostart HLE pronto mas não exercitado; sem menu strings reais |
| M5 | Docs/INDEX; **Gate A permanece RED** |
| R1 | ICALL-BAD `0x40637488` = freelist-as-code; flip morre no open Lgl **antes** do 1º ICALL |
| R2 | HEAP40 skip H3 **DEAD** — silenciar 0x40 **não** restaura flip; H1 MEM dump reforça freelist |
| R3 | único flip issuer = `func_00017ACC` band; content-hold/handler/late-flip **DEAD**; **no re-arm** pós thr |
| R4 | Docs; **Gate A ainda RED** — wall = present re-arm |

---

## Root class (após M0–M4; afiada em M3+ R1–R3)

**Primária (M3, confirmada R3; issuer reclassificado R5–R8):** transição de modo
**intro-present → asset-load** no open `R_LglScA` / FIOS HOST-POP **encerra** o
path de flip. R5: `func_00017ACC` **0 enters** (fingerprint MAINLOOP = residual
SP). R6: issuer real = `_cellGcmSetFlipCommand` NID `0x21397818` via
`func_002EFD60`/`2EFDA4` ← `14FE18` ← … ← `2B2E74`. R7: 2EFD60/14FE18
tot≈29k **post=0** após R_Perm; 2B2E74/B71B8 só 1 enter (boot). R8: hot loop
`CDBA4→CDC08→CDD00→CDD88` ~41k **post=0**; CE0A0=1 (exit `u8[obj+4]` /
movie_idx==4). Após thr o guest **nunca re-agenda** present/pad
(TIMEOUT=120 → SetFlip_after_R_Perm=**0**).

O open WAD é o **início do load** (corte de present esperado); a falha de Gate A
é **falta de re-arm do frame/present pós-load**, não “flip que devia continuar
durante o open”.

**Não é (sozinho / DEAD em R2–R3):**

| Hipótese | Veredito | Prova |
|----------|----------|-------|
| Backend RSX silencioso / GCM handler limpo no WAD | **DEAD** | 25k–31k flips OK pré-WAD; handler/mode 1× cedo, 0 re-sets; guest deixa de chamar SetFlip |
| Content-hold / BOOT_LOGO_MS prende present | **DEAD** | boot logo DONE ×2; ≥1.2k flips **após** último DONE, **antes** do open WAD |
| GetFlipStatus wait hang | **DEAD** | GetFlipStatus count=0 no boot |
| Flip “atrasado” >90s | **DEAD** | TIMEOUT=120 ainda after=0 |
| ICALL-BAD freelist causa única do zero flip | **DEAD (H3)** | corte no open Lgl **antes** do 1º ICALL; HEAP40 skip after=0 |
| Lista TYPE15 vazia | **DEAD** (M2) | CLOSE-PRESERVE nodes=12 — necessário, **não suficiente** |
| Giant-lock / YIELD / LIVE_SKIP | **DEAD** (M3) | medidos inúteis para flip |

**Co-sinais abertos (não resolvidos R1–R3):**

| Sinal | Nota |
|-------|------|
| CC9D0 f4=f54=0 nos 2 objs TYPE15 | writer de avanço (enum 1/5/6/…) não corre; product incompleto |
| parent scene list não cresce | só 2 `this` pós-B71 |
| ICALL-BAD `0x40637488` ×12 | freelist-as-code em `r3=0x400C3D88`, `r12=arena+4=0x40004024` — **co-sinal** stream/UI, **não** causa do corte de flip |
| st620 0→0 pós thr | title / 2nd-movie FSM idle |
| pad poll zero | vive no frame path que não reentra |

---

## M3+ residual R1–R3 (fecho diagnóstico 2026-07-23)

Plano: `ps3recomp/docs/superpowers/plans/2026-07-23-m3plus-postwad-present.md`.

### Commits (gow2-recomp)

| Task | SHA | Mensagem / outcome |
|------|-----|--------------------|
| **R1** | `1a540c8` | `diag(gow2): R1 RE ICALL-BAD e present pós-WAD` — freelist-as-code; flip cut pré-ICALL |
| **R2** | `55e702a` | `diag(gow2): R2 H3 DEAD — HEAP40 não restaura SetFlip_after` — H3 DEAD; MEM dump H1 |
| **R3** | `541c37b` | `diag(gow2): R3 present pós-WAD — mode cut + no re-arm` — único issuer `func_00017ACC`; TIMEOUT=120 after=0 |
| **R4** | `20aeb74` | `docs(gow2): M3+ R1–R3 Gate A RED — present re-arm wall` |

### Commits de suporte (ps3recomp, probes default OFF)

| Task | SHA | Mensagem |
|------|-----|----------|
| **R2** | `e7dce33` | `feat(ppu): PS3_ICALL_HEAP40 + TRACE_ICALL_BAD_MEM (R2 disc)` |
| **R3** | `8eb5e00` | `diag(gow2): R3 PS3_TRACE_MAINLOOP guest flip watermark` |

### Outcomes por task

| Task | Resultado | Aceite Gate A |
|------|-----------|---------------|
| **R1** | `0x40637488` = bloco freelist arena `0x40004020` lido como CTR; não vtable TYPE15; timeline: último SetFlip **antes** `movieio open R_LglScA` | **RED** (diag only) |
| **R2** | `PS3_ICALL_HEAP40=1` → ICALL-BAD 0x40 skip; after=**0**/0 vs base; H3 **DEAD**; MEM: r3 tag freelist `0x9102FFFF` | **RED** |
| **R3** | MAINLOOP: único fingerprint guest `g={17ADE,17ADF}` ∈ `func_00017ACC`; last_flip no open Lgl = total final; D1–D4 DEAD; re-arm **ausente** pós thr | **RED** (SetFlip_after=0 @120s) |
| **R4** | Docs + INDEX; playable=**NO** | **RED** documentado |

### Contagens R3 (representativas)

| Métrica | Valor |
|---------|-------|
| SetFlip_total (intro pré-WAD) | ~25k–31k |
| **SetFlip_after_R_Perm** | **0** (TIMEOUT 90 e **120**) |
| thr_end / R_Perm full | 1 / 1 |
| Gate B | GREEN |
| Flip guest path pré-WAD | só `func_00017ACC` band |
| Flip guest path pós thr | **nenhum** |

---

## Wall seguinte (pós M3+ R1–R12) — síntese honesta Gate A **RED**

**Wall activo = TYPE15 product/tick incompleteness (f4=0) dentro do parent
`2B2DD0→B951C→CC9D0`** — present re-arm **bloqueado** por este spin (ratio 2:1:1
confirmado R12). Menu arm **sem** OPD/schedule vivo (R11). Issuer intro morto
pós WAD (R5–R8).

### Síntese R5–R12 (cadeia de refutações → wall residual)

| Bloco | Tasks | O que morreu | O que ficou |
|-------|-------|--------------|-------------|
| Issuer flip | R5–R7 | 17ACC DEAD; path real `2EFD60`/`14FE18` **post=0** | re-arm ausente |
| Hot intro loop | R8 | `CDBA4…CDD88` ~41k **post=0**; exit CE0A0 | não reentra |
| Menu schedule | R9–R10 | parents/arm B6150/B94* **tot=0**; CE03C wait not sole | arm never |
| OPD | R11 | B94EC/B61F4 no OPD; B6150 OPD orphan; POSTTHR only triangle | schedule DEAD |
| Outer tick | **R12** | 2B2DD0/B951C **não** são bug de gate; B951C **não** chama present; ICALL ctr=`0x27182800` (freelist co-sinal) | **f4=0 residual M2** |

**Não** é: empty-list (M2 nodes=12), pad HLE, HEAP40 default, content-hold,
ICALL-BAD sole cause, circuit-breaker nosso em 2EFD60/CDBA4/menu path (R7–R9),
CE03C wait cosmético (R10), OPD estático B94EC (R11), early-return em 2B2DD0/B951C (R12).

Prioridade de RE/fix:

1. **f4 writers TYPE15** no fluxo natural (enum ∈ {1,5,6,…}; **não** UNSTICK) e/ou
   install de trabalho real nos 12 nodes CLOSE-PRESERVE (M2 residual — freelist
   product, não UI). Critério Gate A: SetFlip_after_R_Perm≥1.
2. Discriminar o que B951C `vt+0x40/+0x44` **deveria** despachar (hoje ctr lixo /
   r3 freelist `0x400C3D88` — co-sinal R1).
3. Só depois: pad poll / menu FSM (M4: autostart inútil sem GetData).

**Não** promover a default: `PS3_CC9D0_LIVE_SKIP`, YIELD, `PS3_ICALL_HEAP40`,
`PS3_TRACE_MAINLOOP`, `PS3_TRACE_FLIPPATH`, `PS3_TRACE_PRESENTLOOP`,
`PS3_TRACE_MENUPRESENT`, `PS3_TRACE_SCHEDARM`, `PS3_TRACE_POSTTHR` /
`PS3_TRACE_POSTTHR_PC` — probes opt-in only.

### R5–R12 (issuer + present-loop + menu schedule + arm + OPD + spin)

| Task | Outcome |
|------|---------|
| **R5** | 17ACC + callers estáticos **tot=0** in-boot; path não é issuer |
| **R6** | Host BT: issuer `func_002EFD60`/`2EFDA4`, NID `0x21397818` |
| **R7** | FLIPPATH: 2EFD60/14FE18/156680 tot≈29k **post=0**; 2B2E74 tot=1; Gate A RED |
| **R8** | PRESENTLOOP: hot=`CDBA4/CDC08/CDD00/CDD88` tot≈41k **post=0**; CE0A0=1; 2C07F8=2 pre-only; exit=u8+4 / movie_idx==4 |
| **R9** | MENUPRESENT: H1 **confirmada** — schedule parents todos post=0; menu path tot=0; 2C0508=6147 pre-only; H4 DEAD; **sem fix** |
| **R10** | SCHEDARM: arm `B6150/B94*` tot=0; `25C838=1` `36A598=0`; BB414→BB37C 1×; CDFDC 11k pre-only; CE03C wait H3 DEAD sole; **sem fix** |
| **R11** | OPD: B94EC/B61F4 **no OPD**; B6150 OPD orphan; POSTTHR 100% {CC9D0,2B2DD0,B951C}; ICALL menu 0; **sem fix** |
| **R12** | RE: 2B2DD0 outer tick → B951C scene half → CC9D0×2; ratio 2:1:1; B951C ICALL ctr=`0x27182800` ≠ present; f4=0; **sem fix** (wall = TYPE15 f4 residual M2) |

Notas: `notes/2026-07-23-r5-17acc-callers.md`, `r6-real-flip-issuer.md`,
`r7-flip-path-rearm.md`, `r8-present-loop-parents.md`,
`r9-menu-present-schedule.md`, `r10-sched-arm.md`, `r11-opd-sched.md`,
`r12-postthr-spin.md`.

---

## Follow-ups Track S / G / M3+ (entrada)

### Track S (shaders naturais) — **entry bloqueada** até flip/present ou entry forçada sem theater

| Item | Aceite alvo | Nota |
|------|-------------|------|
| Nested typemap walk natural pós-R_Perm | ICGLdrShader / `[LDRSH]` N>0 sem stream sintético | historicamente gated em frame/movie mode |
| SHADERSRC non-empty + set_shader hash ≠ demo | Track S + pixels | **não** CRC bypass |
| Planos | `2026-07-21-shader-registry-typemap-walk.md`, `2026-07-20-05-rsx-shaders-pixels.md` | P1 **após** reentrada present **ou** prova de que walk é independente de flip |

### Track G (Metal) — paralelo

- Metal stack M0/M1 overlay+draw path já GREEN no eixo GPU.
- M2+ MSL/tex/state não contam como menu; pixels de jogo exigem draws guest (S/M).

### Track M residual / M3+ — R1–R12 **diag closed** no issuer/loop/schedule/arm/OPD/spin; fix de re-arm **aberto**

| Item | Status | Acção |
|------|--------|--------|
| Present re-arm pós thr | ⬜ wall | depende de TYPE15 f4/work (R12 parent mapeado; sem fix no gate 2B2DD0/B951C) |
| Post-thr spin triangle | ✅ R12 | `2B2DD0→B951C→CC9D0` ratio 2:1:1; B951C ≠ present; f4=0 residual M2 |
| Menu OPD schedule | ✅ R11 | B94EC/B61F4 no OPD; B6150 OPD 0 refs; POSTTHR only CC9D0/2B2DD0/B951C |
| Menu schedule arm | ✅ R10 | B6150/B94* tot=0; 25C838 oneshot 36A598=0; CE03C wait not sole |
| Menu schedule parents | ✅ R9 | H1: 194FFC/2B21*/B61* tot=0; 2C0508/2C07F8 pre-only; H4 DEAD |
| Hot intro loop | ✅ R8 | `CDBA4→…→CDD88→156680` ~41k post=0; CE0A0 exit guards |
| Issuer flip intro | ✅ R6–R7 | `2EFD60`/`14FE18`; 17ACC DEAD (R5) |
| ICALL-BAD 0x40637488 | ✅ classificado | freelist-as-code; H3 DEAD como sole flip cause; writer exacto opcional |
| f4 writers | ⬜ **wall next** | RE estática + store probe; nodes=12 ≠ UI work (M2 residual) |
| Pad/menu | ⬜ blocked | re-smoke só **depois** de flip≥1 pós-R_Perm |

### Theater blacklist (ainda em vigor)

- Sem PARK como StartSeq; sem soft-park CE03C; sem claim menu por FlashUI sparse;
  sem CC9D0-SKIP como menu; sem “playable” com Gate A red; sem HEAP40 default
  como “fix”; sem flip HLE forjado.

---

## Constraints honrados nesta track

- Kill por PID; probes OFF default; sem dados de jogo nos commits.
- Commits PT, sem Co-Authored-By.
- Aceites distinguem in-boot vs offline; **playable não claimado**.
