# A parede do DecodeAu caiu — REG-03 continua PARCIAL, nova parede mapeada (TYPE15/CB56C)

**Data:** 2026-07-31 · **Binário de teste:** `boot_gow2_decodeau_test` (nunca `boot_gow2` de
produção) · **Gate:** `smoke_chain_gate.sh --bin ./boot_gow2_decodeau_test 6`

## Veredicto REG-03: PARCIAL

- **`cellVdecDecodeAu` chamado: MEDIDO, não inferido.** 10 vezes em **6 de 6** corridas —
  contagem directa (`grep -c '\[cellVdec\] DecodeAu #'`), idêntica ao binário de referência
  `boot_gow2.pre_v3` (confirmado nesta sessão, 3/3 corridas, `thr_end=1`). **A parede do
  DecodeAu, objectivo desta fase, está vencida.**
- **`thr_auto_load() end`: continua 0 em 6 de 6.** REG-03 (`thr_end>=1` em `>=4/6`) **não
  fecha**. Não é arredondado para "quase lá": o limiar é `>=4`, o medido é `0`.
- **Causa da parede nova identificada por comparação directa de logs** (referência vs
  teste, mesma janela de execução) — não por inferência: ver secção "A nova parede".

## Tabela medida (referência vs antes da Fase 10 vs depois do Plano 10-01 vs depois do 10-02)

| sinal | referência (`pre_v3`) | antes da Fase 10 (pós-Fase 9) | Plano 10-01 (AREAD-HLE) | Plano 10-02 (+ F2B-STREAM-ALIGN) |
|---|---:|---:|---:|---:|
| `cellVdec DecodeAu` | **10** | **0** | **10** (6/6) | **10** (6/6, mantido) |
| `REPLAY-NOPIC` (skip PICOUT) | 4 | 0 | 4 (6/6) | 4 (6/6) |
| `R_PermA` bytes_read | 20169344/20169344 | 20169344/20169344 | 20169344/20169344 | 20169344/20169344 |
| `FREELIST-TAG-GUARD 262610 entry need=0x31304350` (abort) | — (nunca ocorre) | — | **1x por corrida** (garbage header) | **0x** (eliminado pelo F2B-STREAM-ALIGN) |
| `sys_ppu_thread_create name="AUTO_LOAD"` | **sim** (1x) | não (nunca chega lá) | não | **não** (ainda) |
| `thr_auto_load() end` | 1 | 0 | 0 | **0** |
| `thr_auto_load end >=1` em N/6 (REG-03) | 3/3 (limiar 2) | 0/6 | 0/6 (não medido no 10-01, propositado) | **0/6** |

## O que o Plano 10-01 resolveu (confirmado neste plano)

O patch `AREAD-HLE` (early-out de `func_002B3D1C`, reposto no 10-01) foi suficiente,
sozinho, para o guest voltar a alimentar o decoder: **10 `DecodeAu` em 6/6 corridas**,
número idêntico à referência. Isto fecha o objectivo nomeado da Fase 10 ("Descobrir
porquê [o guest não envia um único AU], e repor o `thr_auto_load`") na sua primeira
metade — a segunda metade (repor o `thr_auto_load`) não fecha nesta fase, ver abaixo.

## Caso B confirmado: parede quebrou, mas há outra a jusante

Com `DecodeAu>=1` em 6/6 e `thr_end<1` em 6/6, o Plano 10-02 seguiu exactamente o Caso B
já mapeado pela Task 1 do 10-01: o bloco órfão de 148 linhas em `func_002BA9BC`
(`F2B-BODY-CLAMP` / `F2B-STREAM-ALIGN` / `F2B-STREAM-RESYNC` / `F2B-STREAM-DESYNC-STOP`),
extraído verbatim de `recomp_macos_v2.pre_v4/ppu_recomp_001.cpp:31664-31811`, instalado
por `patch_f2ba9bc_stream_align_install.py` (novo, idempotente, mesmo contrato atómico
dos outros instaladores F2B).

**Efeito medido, real:** o patch eliminou por completo o abort
`[FREELIST-TAG-GUARD] 262610 entry ... need=0x31304350` que ocorria em toda corrida antes
dele — um header lido perto do fim do R_PermA (banda residual, últimos ~256KB dos
20.169.344 bytes) tinha um tamanho absurdo (~826MB) que a alocação seguinte rejeitava. Nas
6 corridas pós-patch, `[FIOSOPEN] F2B-STREAM-ALIGN residual from=20054912 -> 20038352
(MDL_PUMeterDrain1_0 chain)` dispara exactamente 1x por corrida, e o abort desaparece por
completo. **Isto é uma correcção real, não um no-op** — mas não foi suficiente para
desbloquear `thr_end`.

## A nova parede: TYPE15/CB56C toma um ramo diferente do da referência

Comparando o log de referência (`boot_gow2.pre_v3`, corrida OK) com o log do binário de
teste (pós-10-02) no MESMO ponto — logo após a leitura completa do R_PermA e o segundo
`StartSeq` — a bifurcação acontece aqui:

**Referência** (`chain_gate_boot_gow2.pre_v3_run1_...log:4060-4112`):
```
[POSTINTRO] enter func_000CB56C #1 r3=0x4066D798
[TYPE15] CB56C type high-bit 0x15 -> 0x80000015
[POSTINTRO] CB56C icall1 tab=0x00868D48 idx=0x54 ent=0x47D00000 ...
[TYPE15] CB56C reuse product hdr=0x42F85AE0 prod=0x42F85AE4 (skip icall2; was_shell=1)
[POSTINTRO] CB56C icall2 tab=0x00868D48 idx=0x54 ent=0x47D00000 code=0x0039D428
[POSTINTRO] CB56C obj+8=product=0x42F85AE4 attach=full reused=1
[POSTINTRO] enter func_000CB56C #2 r3=0x4066D804        <- SEGUNDA entrada
... (icall1/icall2 outra vez, reused=1)
[POSTINTRO] B70B0 after 262808 / alloc0x3C0 / 29A3E0 / 263554 / 2B2804 / done
[POSTINTRO] B71 after B70B0 / alloc0x2024 / 393E0 / 251230
[FACTORY] REPAIR obj=0x400D6808 was=0x02000000 -> vt=0x00515700
[FACTORY] freelist REPLENISH ...
[POSTINTRO] B71 icallA ... reuse product r3=0x42F856CC reused=1
[SYS] sys_ppu_thread_create name="AUTO_LOAD" entry=0x00521768 ...
thr_auto_load() start
[cellSaveData] AutoLoad2(...)
```

**Teste (pós-10-02)** (`chain_gate_boot_gow2_decodeau_test_run1_...log:4121-4126`, e depois
directo para `[SPUJOB]`/spin):
```
[TYPE15] CB56C type high-bit 0x15 -> 0x80000015 (match WAD path)   <- ANOTAÇÃO DIFERENTE
[TYPE15] product list RESET prod=0x47D00800 was_head=0x00000000 -> circular empty (2A4FE4-safe)
[TYPE15] shell product vt=0x00200000 prod_src=0x42F85AE4 +2=0x0015
[TYPE15] freelist REPLENISH fl=0x47D00400 mid=0x47D00418 shell=0x47D00800 count=1
[POSTINTRO] CB56C icall2 tab=0x00868D48 idx=0x0 ent=0x40637C08 code=0x00424048   <- idx=0x0, NÃO 0x54
[POSTINTRO] CB56C obj+8=product=0x4F72626F attach=full reused=0                 <- reused=0, produto suspeito
[SPUJOB] spu job returned cleanly  (x7)
[MOVIEFSM] st620 0 -> 0 ...   (repete até ao timeout, nunca converge)
```

**Divergências concretas, medidas:**
1. A referência toma o ramo `icall1` (`idx=0x54, ent=0x47D00000`) ANTES do `icall2`; o teste
   salta directo para `icall2` com `idx=0x0, ent=0x40637C08` — um alvo completamente
   diferente.
2. O teste anota `(match WAD path)` no `TYPE15 type high-bit` — a referência não anota
   nada ali. O guest está a interpretar este objecto como pertencente ao caminho do WAD
   quando a referência não o faz neste ponto.
3. O `product` resultante no teste é `0x4F72626F` — em ASCII, `"Orbo"` às avessas —
   claramente não um ponteiro válido (a referência produz `0x42F85AE4`/`0x42F856CC`,
   ambos endereços de heap plausíveis).
4. O teste nunca atinge a **segunda** entrada de `func_000CB56C` (`#2`), nunca corre o
   bloco `POSTINTRO B70B0`/`B71`/`FACTORY REPAIR`, e por isso nunca chama
   `sys_ppu_thread_create("AUTO_LOAD")` — confirmado por grep directo: essa linha
   **nunca aparece em nenhum dos 6 logs do teste**, enquanto aparece 1x em cada corrida
   de referência.

## Classificação da causa

**Lógica do jogo dependente de estado que ainda não temos** — não é mais um marcador
apagado pelo re-lift (o inventário do 10-01 já cobriu os candidatos conhecidos: `AREAD-HLE`
e o bloco `F2B-STREAM-*`, ambos repostos e confirmados). O ramo `(match WAD path)` de
`TYPE15`/`func_000CB56C` é território **já conhecido e adiado desde a Fase 6**
(`CLAUDE.md`, secção `<deferred>`: "A parede TYPE15 e os probes `29AF0`/`CB56C`
(construídos na Fase 6, à espera de o boot chegar ao `R_PermA` — o que **já acontece**
desde a Fase 9; podem ser reavaliados)"). Esta sessão confirma exactamente essa previsão:
agora que o boot chega ao `R_PermA` E ao `DecodeAu`, o wall TYPE15/CB56C tornou-se
observável e é a próxima parede real — não mais uma suposição.

Não foi tentado nenhum fix para este ramo nesta fase — está fora do âmbito nomeado da
Fase 10 (CONTEXT.md: "O guest abre... e não envia um único AU... Descobrir porquê, e
repor o `thr_auto_load`" — a segunda parte depende agora de uma investigação nova,
TYPE15/CB56C, não do `DecodeAu`).

## Caso C: não aplicável

`DecodeAu` nunca foi 0 depois do Plano 10-01 — o fallback do Caso C (probe gated
`PS3_TRACE_AUFEED` em `cellVdecDecodeAu`) não foi necessário nem instalado.

## O que fica para a próxima fase

- **TYPE15/CB56C, ramo `(match WAD path)` com `idx=0x0/ent=0x40637C08`**: localizar em
  `func_000CB56C` (e no dispatcher que decide `icall1` vs saltar direto a `icall2`) porque
  o guest toma este ramo agora que chega aqui pela primeira vez com o R_PermA completo —
  provavelmente um campo de estado (talvez o mesmo `type_sys`/`+0x1A8` da cadeia F2B, ou
  um campo do objecto `0x4066D798` equivalente) que difere entre o binário de referência e
  o actual.
- Os probes `29AF0`/`CB56C` da Fase 6 (adiados) são o ponto de partida natural — já
  existem, e agora o boot alcança-os de verdade.
- **REG-04** (a correcção sobrevive a um re-lift do zero) só pode ser avaliado depois de
  REG-03 fechar — continua bloqueado por este mesmo motivo.

## Artefactos desta sessão

- `patch_f2ba9bc_stream_align_install.py` (novo, `recomp_mid_v2/`): repõe
  `F2B-BODY-CLAMP`/`F2B-STREAM-ALIGN`/`F2B-STREAM-RESYNC`/`F2B-STREAM-DESYNC-STOP` em
  `func_002BA9BC`. Efeito medido: elimina o abort `need=0x31304350` do
  `FREELIST-TAG-GUARD`, 6/6 corridas. Idempotente (`APPLIED` -> `ALREADY`), compile-check
  isolado sem erro de símbolo em falta.
- `boot_gow2_decodeau_test` (gitignored, binário de teste, nunca produção).
- `/tmp/chain_gate_decodeau_test.tsv` (6 corridas, pré-F2B-STREAM-ALIGN) e
  `/tmp/chain_gate_decodeau_test2.tsv` (6 corridas, pós-F2B-STREAM-ALIGN) — ambos com
  `elo_stopped=AUTO_LOAD (thr_end)` em 6/6, `class=REGRESSAO`.
- `/tmp/chain_gate_reference_check.tsv` (3 corridas contra `boot_gow2.pre_v3`,
  `thr_end=1`/`elo_stopped=nenhum`/`class=OK` em 3/3) — confirma que o alvo é real e
  alcançável, não hipotético.
