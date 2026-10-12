# AUTO_LOAD SIGSEGV = cellSaveData guest-ptr-as-host (menu wall unblocked) — 2026-07-22

## O que estava travando (H1 "menu phase")
Pós-`exit B71B8 +0x64=1`, o jogo cria o thread **AUTO_LOAD** (OPD `0x521768` →
code `func_00147038`, toc `0x541178`, arg `0x57EBD8`) para carregar o próximo
estágio. Uma sessão anterior **stubou** AUTO_LOAD em `sys_ppu_thread_create`
("guest entry 0x521768 crashes host (SIGSEGV)") para provar ausência de FATAL
pós-exit. Com o stub, o estágio nunca carrega → main loop gira `st620 0→0` pra
sempre. **O stub ERA o hang do menu.** (O `need=0x687DD790`/freelist que o
utilizador lembrava já estava vencido; o hang tinha avançado para cá.)

## Causa raiz do SIGSEGV (crash report do utilizador, thread 26)
```
_platform_strlen  →  __vfprintf  →  printf
  → cellSaveDataAutoLoad2 + 92        (libs/system/cellSaveData.c)
  → ps3_hle_call → ps3_import_thunk
  → func_00147038 + 1548             (corpo do thr_auto_load)
EXC_BAD_ACCESS far=0x004C4CE0  (x0=x1=x19=0x4C4CE0)
```
`0x4C4CE0` é um **EA guest** (rodata no segmento de código `0x10000–0x50C7E0`).
`cellSaveDataAutoLoad2` fazia `printf("...dir='%s'", dirName)` com `dirName`
= **EA guest cru**, passado direto ao `printf` do host → `strlen(0x4C4CE0)` no
espaço do host → endereço não mapeado → SIGSEGV.

**Convenção (ppu_hle.cpp):** `ps3_hle_call` passa os GPRs r3..r10 **sem
traduzir** ponteiros. Toda função HLE que deref um ponteiro-arg como memória
host DEVE traduzir com `vm_to_host()` (é o que `sys_ppu_thread_create` faz para
o nome do thread). `cellSaveData` (e `cellScreenshot`) não traduziam.

## Fix (libs, persistem — não são ppu_recomp/gitignored)
Helper `savedata_dir_host()` + tradução no topo dos **5 entry points** que
recebem `const char* dirName`:
`cellSaveDataAutoLoad2 / AutoSave2 / AutoLoad / AutoSave / Delete`
(`libs/system/cellSaveData.c`). `dirName = vm_to_host(EA)` cobre printf,
`build_save_path` (snprintf %s) e `marshal_statget_init` (strnlen/memcpy).

Same-class (auditoria — utilizador pediu "corrige os parecidos"):
- **`cellScreenShotSetOverlayImage`** (`libs/misc/cellScreenshot.c`): `srcDir`/
  `srcFile` guest em `printf %s` **e `strncpy`** → traduzidos. Idêntico. (Não
  exercitado no boot do GoW2; fix por paridade.)
- **`cellOskDialogLoadAsync`** (`libs/misc/cellOskDialog.c`): deref
  `inputFieldInfo->message` trata um **ponteiro de STRUCT guest** como host —
  same-class mas exige marshaling BE do struct + tradução de campo. **Sinalizado,
  NÃO corrigido** (mais arriscado, fora do path do GoW2). Follow-up.

## Prova in-boot (PS3_AUTO_LOAD_RUN=1, sem lldb)
```
[THREAD 15] host thread started, entry=0x00521768
thr_auto_load() start
[cellSaveData] AutoLoad2(version=0, dir='BCUS98229_GOW2')   ← dir REAL (antes: crash)
[cellSaveData] dispatching funcStat OPD=0x00521798 (isNew=1)
[cellSaveData] funcStat returned cbResult.result=-4
```
`boot AINDA VIVO 50s (sem crash)`. O callback `funcStat` do **próprio jogo**
executou — lógica real pós-B71 a correr pela primeira vez.

## Ainda aberto (próxima parede — NÃO é o crash)
`funcStat` retorna `cbResult.result=-4` → `AutoLoad2` retorna `0x8002B40B`
(`CELL_SAVEDATA_ERROR_CBRESULT`) → o jogo entra num caminho de erro:
`[tty] suspicious len=65536 clamped` + varredura `[vm] OOB access 0xE0000000,
+8, +8…` (loop sobre base inválida, guardado) → novo spin.
Hipótese: o `StatGet` marshalado está incompleto (`getParam`/PARAM.SFO a zero;
ver `marshal_statget_init`) e/ou o mapeamento de `cbResult` para o LOAD com
`isNewData=1` faz o jogo tratar como erro em vez de "novo save". Próximo alvo.

## Gate
`sys_ppu_thread.c`: `PS3_AUTO_LOAD_RUN=1` desliga o stub (deixa AUTO_LOAD correr).
Default ainda stubado — decidir se flipa o default agora que o crash morreu
(CLAUDE do motor: remover breaker obsoleto com prova de não-regressão do
baseline intro; AUTO_LOAD é pós-B71, não toca a intro).

## Estabilidade (batch 3× build integrado, AUTO_LOAD destubado, 45s)
| Run | Resultado | SEGV | AUTO_LOAD | OOB E0 | tty-loop |
|-----|-----------|------|-----------|--------|----------|
| 1 | WADLD-STALL (stream FULL, B71 não saiu) | 0 | — | 0 | 0 |
| 2 | STALL-PRE-B71 (stream não completou) | 0 | — | 0 | 0 |
| 3 | **AUTOLOAD-OK limpo** | 0 | ✅ | 0 | 0 |

- **Zero SIGSEGV em 3/3.** Crash morto.
- Quando chega ao AUTO_LOAD (RUN 3): **caminho save-data 100% limpo** — o userdata
  fix (CBResult+0x10) + dirName fix eliminaram o OOB `0xE0000000` e o tty-loop 65536.
- **Bloqueador de estabilidade restante = flakiness do WADLD `41D5C`** (~2/3 travam):
  stream-desync pós-multi-MB body. Mesmo com EOF-DONE (state=0, file_pos FULL), o
  wait do guest em `41D5C→002BA808→0025C0D4` (poll de delta-time) não sai — espera um
  membro WAD que o stream dessincronizado não produziu. **Pré-existente, ortogonal a
  este fix**, batalha em curso (ver `f2b-multimb-stream`, `factory15-393e0-desync`,
  `postintro-wadld-sm-hang`). Próximo alvo para "versão estável" = determinismo do
  parse de header após o corpo multi-MB.

## WADLD 41D5C deadlock — LOCALIZADO (não é desync de stream)
Ataque ao bloqueador de estabilidade (`PS3_TRACE_BA808=1`, probe no topo de
`func_002BA808`):
- O SM `type_sys=0x00869570` **completa** (EOF-DONE ok): probe mostra
  `state=0 f1B0=0 rem=0 body1D4=0` — **tudo zero, idle**. Hipótese inicial
  (EOF-DONE não limpa `0x1B0`) **refutada**: o `0x1B0` desse objeto já é 0.
- Mesmo assim o loop `func_002BA824 ↔ func_002BA808 ↔ func_0025C0D4` gira
  **111M×** sobre esse objeto idle. `func_0025C0D4` = conversão timebase→tempo
  (`ps3_timebase_now` + float rate); o loop compara **deadline `gpr26` vs tempo
  decorrido `gpr3`** e nunca satisfaz.
- **É flaky (1/3 passa — RUN 3 chegou ao AUTO_LOAD limpo)** → **corrida/timing**
  (deadline vs evento/flag de outra thread/SPU), **não** conteúdo de stream.
  447 SPU jobs retornam limpos; stream FULL sempre. O alvo do fix é a semântica
  do timed-wait/race, não o parse do WAD.
- **Condição exata do spin** (`PS3_TRACE_BA808`): `o=0x869570 state=0 f1B0=0
  1C8evt=0 deadline26=0xFFFFFFFFEC9735B8 gpr27=0`. O deadline é **sign-extended**
  e a cmp lift é **UNSIGNED** → timeout ~infinito; o evento `[0x1C8]` nunca é
  setado (dispara por corrida ~1/3 dos boots).
- **Fator relacionado — SPU MISS:** no tail aparecem `[spu_workload] dispatch
  MISS fp=0x9527C889B1945669 size=38504` e `fp=0x3512A7E99D34E0FF size=54544`
  (**não** são spu1/2/3 `0x2A5C…/0xABCD…/0xED6A…` — são imagens SPU **não
  liftadas**). PORÉM o run AUTOLOAD-OK também teve ~110 MISS e progrediu → o
  jogo **tolera** o MISS; não é deadlock duro. MISS correlaciona com o stall mas
  a causa direta é a corrida deadline/evento.
- **Tentativas de fix (revertidas):** (1) limpar `0x1B0` no EOF-DONE = no-op
  (já era 0 nesse objeto); (2) setar `0x1C8=1` no EOF-DONE = quebra o wait mas o
  jogo entra em **re-load loop** (~70× stream FULL) — pior. Ambas revertidas.
  Sinal de "questionar a arquitetura" (3 tentativas): a conclusão da WAD-load é
  multi-passo (SM idle + evento + SPU) e não fecha com um único campo.
- **Teste do deadline (revertido):** forçar "timeout expirado" quando `gpr26<0`
  (gated `PS3_WADLD_TMO`, em `func_002BA808`+`func_002BA824`, 2 cópias/lifter dup)
  **não fixou** — os stalls viraram **STALL-PRE-B71 (stream nem completa)**: o
  override dispara **antes** do EOF e quebra a própria WAD load. Ou seja, o
  deadline-do-wait e a conclusão-do-stream estão **entrelaçados** → não é bug de
  cmp isolado; o deadline negativo no EOF pode ser legítimo (jogo usa o evento
  `[0x1C8]`, não o timeout). **4 band-aids (0x1B0/0x1C8/override×2) = questionar a
  arquitetura** (regra do systematic-debugging). Tudo revertido.
- **Conclusão:** WADLD `41D5C` é **multi-modo** (corrida de conclusão do stream +
  deadline do wait + evento `[0x1C8]` + 2 SPU não-liftados). Precisa de trabalho
  arquitetural/colaborativo, não band-aid. Frentes reais: (a) por que o evento
  `[0x1C8]` só dispara ~1/3 (quem o seta? SPU? thread SMPD?); (b) liftar SPU
  `0x9527C889B1945669`/`0x3512A7E99D34E0FF`; (c) por que ~1/3 o stream nem
  completa (STALL-PRE-B71). Probe deixada gated-OFF: `PS3_TRACE_BA808`
  (`ppu_recomp_005.cpp func_002BA808`).
- 0x4E153E3E (`cellSpursGetWorkloadInfo`) agora **resolvido** (co-edit) — build
  destravado com `typedef struct CellSpursWorkloadInfo` em `libs/spurs/cellSpurs.h`.

## RENDER ALCANÇADO + fixes de timing + próximo muro (menu)

**WADLD 41D5C — RESOLVIDO (determinístico).** Fix EOF-gated: quando a stream
drena (`f2b_stream_eof_try_complete` seta `g_wadld_eof_ea=type_sys`), o wait
`func_002BA808/BA824` completa pelo **timeout path** (`loc_002BA854`, não o
re-load do evento). Batch **3/3 AUTO_LOAD** (era ~1/3). **Default on**; desliga
com `PS3_WADLD_NO_EOF_EXIT=1`. Sites: `ppu_recomp_001.cpp` (global+EOF-DONE),
`ppu_recomp_002/005.cpp` (força cmp no wait).

**Timebase rebase (raiz sistemática).** `ps3_timebase_now()` POSIX usava
`clock_gettime(CLOCK_MONOTONIC)` **absoluto** — no Darwin conta desde o boot
(~400000s), gerando timebase gigante → deadlines do guest overflow/sign-extend
(o `0xFFFFFFFFEC9735B8` do WADLD). Fix: rebasear ao 1º call (igual ao branch
Windows). `runtime/syscalls/sys_timer.c`.

**AUTO_LOAD completa limpo** (WADLD fixo + seu userdata/CBResult + first-run
NODATA→CELL_OK): `thr_auto_load() start … AutoLoad2(dir='BCUS98229_GOW2') …
CELL_OK (new game) … thr_auto_load() end`. `cellSpursGetWorkloadInfo` (NID
0x4E153E3E) resolvido.

**RENDER REAL via Metal** (`PS3_RSX_BACKEND=metal PS3_FRAME_DUMP=1`): window
1280x720, draw PSO, **logo da Bluepoint Games desenhado pelo jogo** (não demo).
Frames dumpados. → primeiro conteúdo REAL do jogo na tela.

**Próximo muro (menu não renderiza):** pós-AUTO_LOAD o jogo **congela no logo
Bluepoint** (frontend). Sample: 6 threads em `sys_lwcond_wait →
ppu_giant_lock_acquire_prio` (acordadas, inanição do giant lock) + main girando
`func_000CC9D0`. Conds criados: **`schedul`** (SPURS scheduler ×3), `dearchi`,
`opWait`. Correlaciona com **2 SPU images NÃO-LIFTADAS** que o scheduler despacha
e dão MISS: `fp=0x9527C889B1945669` (38504 B) e `fp=0x3512A7E99D34E0FF`
(54544 B) — **dumpadas** (`PS3_SPU_DUMP=1` → `spu_miss_*.bin`, cópia em
scratchpad). O workload SPU nunca completa → cond `schedul` nunca sinaliza →
frontend deadlocka → menu não carrega.

**Caminho pro menu — LIFT PROVADO.** As 2 SPU images dumpadas são **SPU ELFs
válidos** (EM_SPU=23). `spu_lifter.py <bin> --auto-functions <bin>
--symbol-prefix spuN_ -o spu_lifted/spuN_v2` liftou ambas:
- `0x9527C889B1945669` (e_entry 0x3050): **515 funções**, 84% cobertura, só 15
  `.word` (dados no .text).
- `0x3512A7E99D34E0FF` (e_entry 0x3070): **448 funções**.
Registro em `recomp_mid_v2/gow2_spu_register.c`: `extern spuN_spu_func_<entry>`
+ `spuN_spu_recomp_register()` + `spu_workload_register(fp, spuN_spu_func_<entry>,
"gow2_spuN")` gated `PS3_SPU4`/`PS3_SPU5`. `build_macos.sh` já globa `spu?_v2`.
**Nota:** as pastas `spu4_v2`/`spu5_v2` foram marcadas `.BROKEN` (co-edit — em
revisão/integração pelo autor). Ressalva conhecida: lifted SPU pode faltar
mid-run (SEH/setjmp isola o job; spu2/3 são opt-in por isso). Uma vez que spu4/5
rodem sem fault e sinalizem o cond `schedul`, o frontend destrava → menu.

## SPU lift integrado — mas RED HERRING pro menu; muro real = giant lock

**spu4/spu5 liftados + integrados (opt-in):** as 2 images MISS (`0x9527…`/`0x3512…`)
são SPU ELFs válidos; `spu_lifter.py --symbol-prefix` liftou (515/448 fns). Faltava
o helper **`spu_pref_u32`** em `runtime/spu/spu_helpers.h` (rchcnt→slot preferido)
— por isso as pastas estavam `.BROKEN` (não compilavam). Helper adicionado → ambas
compilam; registradas em `gow2_spu_register.c` gated `PS3_SPU4/PS3_SPU5`.
In-boot com PS3_SPU4/5: **MISS zerado, ~450 SPUJOB "returned cleanly", 40 HIT** —
os jobs RODAM. **Mas flaky:** um job pode faltar (SIGSEGV/SIGBUS não pego pelo
setjmp no Darwin → mata o processo, ~19s) — mesma razão de spu2/3 opt-in.

**PORÉM: com os SPU rodando limpos, o jogo AINDA congela no logo Bluepoint.**
Logo o SPU MISS **não era o bloqueador do menu**. Red herring.

**Muro real do menu = inanição do GIANT LOCK.** Sample pós-AUTO_LOAD: 6 threads em
`sys_lwcond_wait → ppu_giant_lock_acquire_prio` (acordadas mas não re-adquirem o
lock) + main girando em `func_000CC9D0` (125 linhas, sem yield/syscall)
**segurando o giant lock**. A main espera um resultado dos workers; os workers
não conseguem o lock (main não libera). Deadlock circular. `func_000CC9D0` é
poll puro sem store → não dispara o store-preempt do giant lock → não cede.
Próximo alvo: fazer o poll da main ceder o giant lock (preempt point / yield)
OU achar o flag que ela espera e por que o worker que o seta está faminto.
Isto é threading/arquitetura, não SPU nem stream.

## RAIZ DO FREEZE: lost-wakeup no lwcond POSIX (gap só-Windows) — DESTRAVADO

O freeze do frontend NÃO era SPU nem giant-lock (ambos red herrings). Era um
**lost-wakeup no `sys_lwcond_wait` POSIX** (`libs/system/sysPrxForUser.c`). O
branch `#ifdef _WIN32` tinha DOIS fixes que o `#else` (macOS) **nunca teve**:
1. **Gen-slice**: Windows fatia a espera e re-checa `gen` a cada passo; POSIX
   fazia `pthread_cond_wait` puro. Um `pthread_cond_signal` que dispara antes do
   waiter parar no cv → wakeup perdido E gen nunca re-checado → **espera eterna**.
   Os 9 workers do SPURS scheduler (`func_00463598`) travavam num cond já
   sinalizado. Fix: `pthread_cond_timedwait` fatiado (50ms) + re-check de gen.
2. **Re-acquire do lwmutex guest**: Windows faz CAS FREE→owner na volta; POSIX
   deixava o mutex FREE → "attempt to unlock invalid mutex '(null)'" → FIOS/movie
   scheduler travava. Fix: CAS re-acquire (espelha o Windows).

**Efeito in-boot:** o jogo saiu do freeze e **toca o intro real do GoW2** (cena
atmosférica de partículas, 330 frames RGBA via VideoToolbox) → boot logo queue
`scep_e.ctxr` (**logo SCEA**) → próximo cellVdec. A sequência de logos/intro
avança pela primeira vez. (Auditoria: os únicos gaps FUNCIONAIS só-Windows eram
esses 2; o resto dos blocos `#ifdef _WIN32` é diagnóstico/glue — MOVIEPOLL
backtrace, cvcs init, CriticalSection.)

## Code sites
| Peça | Onde |
|------|------|
| Crash (fixado) | `libs/system/cellSaveData.c` 5 entries + `savedata_dir_host` |
| Same-class fix | `libs/misc/cellScreenshot.c` `cellScreenShotSetOverlayImage` |
| Same-class flagged | `libs/misc/cellOskDialog.c` `cellOskDialogLoadAsync` |
| Stub + gate | `runtime/syscalls/sys_ppu_thread.c` (`PS3_AUTO_LOAD_RUN`) |
| Convenção HLE (ptr cru) | `runtime/ppu/ppu_hle.cpp` `ps3_hle_call` |
| AUTO_LOAD OPD | e_entry-class OPD `0x521768` → code `0x147038` |
