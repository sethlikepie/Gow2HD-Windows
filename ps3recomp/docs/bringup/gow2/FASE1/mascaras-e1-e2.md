# Fase 1 — Experimentos E1/E2: o que cada máscara engole e o que muda sem ela

Task 6 do plano `2026-07-25-fase1-desmascarar-memoria`. Consome as Tasks 1–5
(módulo `ps3_mask_trace`, `vm_uncommitted_guard` e `tblsize_guard` ligados ao
canónico `MEMORY | SWALLOWED`, mapa de regiões corrigido em `vm.h`, registo
de ranges comitadas em `sys_memory.c`). Não introduz código — só mede e
relata. Rótulos usados em cada afirmação: **in-boot** (observado num run real
de `boot_gow2`), **offline-unit** (suíte/teste isolado) ou **não-exercitado**
(caminho que existe mas não foi accionado nesta janela).

## Ambiente e metodologia

- Worktree: `ps3recomp-bringup`, branch `bringup/fase1-desmascarar-memoria`,
  HEAD `3098ebd8` (sem alterações de código nesta task).
- `boot_gow2` recompilado com
  `PS3_ENGINE_ROOT=/Users/andrebrumcortezferreira/Documents/PESSOAL/ps3recomp-bringup ./build_macos.sh`
  a partir de `../gow2-recomp` — garante que se mede este worktree, não o
  motor de outro checkout (regra G6). Build limpo, 0 erros.
- Todos os runs: `PS3_NO_RSX=1 PS3_RSX_BACKEND=trace` + `env_gow2.sh` (WAD
  R_PermA real, SPU1 ligado, VDEC async). `PS3_TRACE_MEMORY=1` em todos.
- Morte sempre por PID (`TERM`, esperar, `-9` se preciso); nunca `pkill -f`.
- `vm commit (sys_memory window) failed` (flake conhecida): **0 ocorrências
  em nenhum dos 5 runs** (E1, controlo, E2-a, E2-b, E2-b-longo) — **0
  repetições** foram necessárias por causa dela.
- Máquina partilhada: durante o run autoritativo (com `exec`) do E2-b (ver
  nota metodológica abaixo) apareceu um segundo processo `boot_gow2` (PID
  84355, `PPID=84345`) que **não é meu** — não lhe toquei, confirma o aviso
  do prompt de que a máquina está partilhada com outra sessão.

### Nota metodológica: um erro operacional próprio, corrigido em runtime

Nos dois primeiros runs de E2 (E2-a e a primeira tentativa de E2-b) lancei o
subshell **sem `exec`** antes de `./boot_gow2 EBOOT.ELF` (queria capturar o
`$?` a seguir). Isso significa que `$!` era o PID do *subshell*, não do jogo.
Para o E2-a não teve consequência — o processo morreu sozinho (SIGBUS) antes
de eu precisar de o matar. Para a primeira tentativa de E2-b, matar o
subshell por `TERM` **não matou o `boot_gow2` filho**: ficou órfão
(reparentado para `PPID=1`) a correr sem supervisão durante vários minutos,
consumindo ~100% CPU, até eu o detectar via `ps aux` (PID 80355) e o matar
por PID assim que percebi o que tinha acontecido (nenhum órfão ficou para
trás depois disso — confirmado por `ps aux` vazio). Repeti o E2-b com
`exec ./boot_gow2 EBOOT.ELF` (o mesmo padrão usado em E1/controlo, onde o
`TERM` atinge o jogo directamente porque o `exec` substitui a imagem do
subshell) — esse é o run **autoritativo** citado na secção do `tblsize_guard`
abaixo. O log da primeira tentativa (contaminado por ter corrido ~5 min sem
supervisão, não ~100 s) foi preservado, não apagado, como
`f1_e2_tblsize_run1_LEAKED_no_exec.log`, e é citado só como confirmação
suplementar (não muda nenhuma conclusão — ver essa secção).

### Logs preservados (scratchpad, fora dos dois repos, não apagados)

```
f1_e1.log                              (E1, mascaras ligadas, ~115s, 29722 linhas)
f1_e2_controlo.log                     (E2 controlo, ~102s, 24983 linhas)
f1_e2_uncommitted.log                  (E2-a, PS3_MASK_OFF_UNCOMMITTED=1, autoterminou aos ~25-30s, 25477 linhas)
f1_e2_uncommitted_lldb.log             (tentativa de backtrace via lldb, abandonada — ver nota)
f1_e2_tblsize.log                      (E2-b autoritativo, com exec, ~102s, 26834 linhas)
f1_e2_tblsize_run1_LEAKED_no_exec.log  (E2-b primeira tentativa, órfã, ~5min, 30102 linhas — suplementar)
f1_e1.json / f1_e2_controlo.json / f1_e2_uncommitted.json / f1_e2_tblsize.json
                                        (saída de tools/trace_parse.py --json)
```

---

## Máscara 1 — `uncommitted_guard`

### Quanto engole (E1, in-boot)

`grep -c 'mask=uncommitted_guard' f1_e1.log` → **81 linhas** (64 `SWALLOWED`
completas até ao cap + 17 `SWALLOWED_TRUNCATED`). O último marco de
truncagem antes do kill (~115 s de parede):

```
[PS3T] 6440355398 | ... | SWALLOWED_TRUNCATED | mask=uncommitted_guard count=4194304
```

**Contagem real: limite inferior de 4.194.304 (2^22)** — o próximo marco
(8.388.608) nunca chegou a ser escrito antes do kill; o valor verdadeiro está
entre os dois. O run de controlo (mesma configuração, run separado, ~102 s)
reproduziu **exactamente** o mesmo padrão: 81 linhas, mesmo último marco
`count=4194304`, mesma distribuição de EAs e funções abaixo — o bug é
determinístico dado o mesmo caminho de boot, não ruído de amostragem.
**Rótulo: in-boot**, confirmado em 2 runs independentes.

**Confirmação empírica da cadência geométrica (carry-forward da Task 2):** a
Task 2 tinha provado a cadência geométrica só por aritmética e por teste
sintético de 257 eventos. Este é o primeiro log real que a exercita em
escala: **17 linhas `SWALLOWED_TRUNCATED`** cobrem ~4,19 milhões de eventos
(marcos exactos: 65, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768,
65536, 131072, 262144, 524288, 1048576, 2097152, 4194304). A **mensagem de
despacho desta task** (não o `task-6-brief.md`, que não quantifica isto)
estimava "~16 linhas" — a medição real deu 17 (a mesma ordem de grandeza; a
diferença de 1 vem do cap inicial em 64 mais o primeiro marco em 65
coincidirem por desenho). Contra as **65.056** linhas `SWALLOWED_TRUNCATED`
que a agenda linear teria produzido para o mesmo volume — comparação
como-para-como, só linhas `TRUNCATED`, excluindo as 64 `SWALLOWED` do cap
que são iguais nas duas agendas (a Task 2 media o total incluindo o cap
como 65.120 = 64 + 65.056; o valor a comparar com as 17 `TRUNCATED` de
agora é 65.056, não 65.120) — a redução é real e confirmada in-boot, não só
por teste sintético.

### Top de funções guest atribuídas (das 64 `SWALLOWED` completas, in-boot)

| Função (símbolo como aparece) | Ocorrências |
|---|---|
| `_Z13func_002BAC48P11ppu_context+0x198` | 15 |
| `_Z13func_002BAC60P11ppu_context+0x74` | 9 |
| `_Z13func_002B2C0CP11ppu_context+0x15C` | 8 |
| `_Z13func_002BB41CP11ppu_context+0x1E0` | 6 |
| `_Z13func_002B2C0CP11ppu_context+0x31C` | 6 |
| `_Z13func_002ACD38P11ppu_context+0x114` | 6 |
| `_Z13func_002ACBE8P11ppu_context+0x12C` | 6 |
| `_Z13func_002AB1ACP11ppu_context+0x3B4` | 6 |
| `_Z13func_002635A4P11ppu_context+0x20` | 1 |
| `_Z13func_00263554P11ppu_context+0xD4` | 1 |

À medida que o volume cresce (marcos `SWALLOWED_TRUNCATED` acima do cap), o
chamador dominante passa a ser `_Z13func_000B951CP11ppu_context+0x404` —
presente em todos os marcos a partir de `count=32768`. **Não confirmei a
identidade desta função com o alocador free-list "megaprojecto" do
`CLAUDE.md`**: o `CLAUDE.md` nomeia `func_002550C8`/`func_00255178` (o
fix do fallthrough), e o comentário de `ppu_loader.cpp:547-548` nomeia
`func_00263178 et al.` como o alocador free-list que dereferencia o
ponteiro tagueado — nenhum dos dois cita `func_000B951C`. Pode fazer parte
da mesma cadeia de chamada (o "et al." deixa a porta aberta), mas isso não
foi verificado por disassembly ou callgraph nesta task. **O facto medido,
sem inferência de identidade, é**: o chamador dominante acima do cap é
`func_000B951C+0x404`. **Rótulo: in-boot** (2 runs, mesmo padrão; a
identidade com "o alocador" é hipótese não confirmada, não facto medido).

### Distribuição por região

**100% (64/64) das linhas `SWALLOWED` completas: `region=unmapped`.** Isto
já não é o "unmapped" mentiroso que a Task 4 corrigiu (o mapa de `vm.h` já
inclui `lv2_heap`, `sys_vm`, `rsx_local` e `tls`) — os EAs observados
(banda `0x80000000`–`0x9FFFFFFF`) caem genuinamente fora de qualquer região
definida no mapa (não `lv2_heap` 0x40000000–0x60000000, não `sys_vm`
0x60000000–0x70000000, não `rsx_local` 0xC0000000–0xD0000000). `region=main`
aparece só na máscara `tblsize_guard` (ver abaixo) — **nunca**
`region=rsx_local`: o rótulo continua **não-exercitado** em trace real
mesmo depois desta medição (carry-forward da Task 4 confirmado, não
refutado — a lógica de `rsx_local` está provada só offline-unit).

EAs observados nas 64 linhas completas: 38× `0x91030047`, 12× `0x9502BD0F`,
12× `0x9502BD0B`, 1× `0x8000001A`, 1× `0x80000008` — todos na banda que o
próprio `ppu_loader.cpp:636` documenta como **"allocator garbage-pointer
signature"** (0x80000000–0x9FFFFFFF). **Rótulo: in-boot.**

### Até onde o boot chega sem a máscara (E2-a, `PS3_MASK_OFF_UNCOMMITTED=1`)

**Crash observado (n=1), consistente com a previsão do comentário do
código (`ppu_loader.cpp:571-582`, Task 2 Round 1) — mecanismo não
confirmado.** O processo morreu sozinho por sinal, `exit code 138 =
128+10 = SIGBUS` (Darwin), numa **única execução** — não repeti o E2-a. Isto
contrasta com o padrão em "Quanto engole" acima, que está confirmado em
**2 runs independentes** (E1 e controlo) com a máscara ligada: a força da
evidência não é simétrica entre as duas metades deste experimento. Não
precisei de matar o processo.

Onde parou, comparado ao controlo:

| | Controlo (máscara ligada) | E2-a (`MASK_OFF_UNCOMMITTED=1`, n=1) |
|---|---|---|
| `st620` | oscila 0→1→3→11→0 (intro toca, overlay termina) | mesma sequência 0→1→3→11→0 observada, **confirmada concluída na linha 4944 — muito antes da linha 25473/25477 do crash**, não extrapolada a partir de um run mais curto |
| WAD R_PermA | completo, `bytes_read=20169344/20169344` (linha 24453) | completo, `bytes_read=20169344/20169344` (linha 25473) |
| Depois do WAD | 5 linhas depois: começa o burst de `mask=uncommitted_guard` (primeiro EA `0x91030047`, linha 24458) | **4 linhas depois: `###EXITCODE=138###`** (linha 25477) |

O crash acontece **exactamente no mesmo ponto do log** (medido por posição
relativa ao evento `[GATE-FORCE] R_PermA full flagged`, não por tempo de
parede — os dois runs têm velocidades de parede diferentes por variação de
carga da máquina) onde o controlo começa a queimar o guard no ponto de
disparo do alocador free-list contra `0x91030047`. Isto é **circunstancial
mas muito forte**: mesmo ponto de log, mesmo subsistema (o mesmo
`[GATE-FORCE]` que precede o burst em ambos os runs), mesma banda de EA já
identificada no código como assinatura do ponteiro-lixo.

**Discriminadores que tentei e que não estão disponíveis:**

- **Backtrace/core dump automático do SIGBUS.** O macOS escreve um `.ips`
  com backtrace e endereço de falta em `~/Library/Logs/DiagnosticReports/`
  quando um processo morre por sinal. Verifiquei: os `boot_gow2-*.ips`
  mais recentes são de 22 de Julho — **nenhum do crash de hoje**. Essa
  ausência é ela própria um dado, não um beco morto ignorado: é consistente
  com o runtime instalar handlers de sinal próprios (a mesma causa,
  plausivelmente, que bloqueou a tentativa seguinte com o `lldb` — ver
  abaixo), o que impediria o `ReportCrash` do sistema de intervir a tempo
  de escrever o relatório.
- **`lldb`** (`run` + `thread backtrace` + `register read`): abandonado ao
  fim de ~140 s sem sequer produzir o primeiro byte de output do
  processo-filho (o runtime usa sinais internamente para a escalonagem de
  prioridade do giant-lock documentada no `CLAUDE.md`; o `lldb` por omissão
  pára em qualquer sinal não reconhecido, e nunca cheguei a configurar os
  `process handle` necessários dentro do orçamento desta task). Morto por
  PID, sem órfãos.

**Hipóteses alternativas que não excluí** (nenhuma delas é "o guard
deixaria de interceptar este acesso preciso e foi ele que matou o
processo" — essa é só a mais óbvia, não a confirmada):

(a) o crash pode ter-se originado no **próprio gate novo**
    (`ps3_mask_disabled(PS3_MASK_UNCOMMITTED)` e o código à sua volta,
    das Tasks 1–5 desta fase — código que nunca tinha corrido em produção
    antes desta task, sem exercício prévio na mesma escala);
(b) pode ser **a jusante** do primeiro acesso que o guard deixaria de
    interceptar, não nesse acesso em si — por exemplo um acesso
    subsequente que só se tornou alcançável por o primeiro ter "passado"
    em vez de ser engolido.

Sem backtrace, não posso descartar nenhuma das duas. **Rótulo desta
correlação: in-boot (n=1), por posição de log + código de saída — o
mecanismo causal exacto não está confirmado por backtrace directo.**

### Veredicto — `uncommitted_guard`

**Esconde corrupção real do jogo (ponteiro-lixo do alocador guest), não
compensa um bug nosso de mapeamento.** Quatro razões, todas fundamentadas
nesta medição ou nas anteriores desta mesma fase:

1. A banda `0x80000000`–`0x9FFFFFFF` está fora de qualquer região válida do
   PS3 (não é heap, não é RSX, não é stack) — não é um buraco no mapa que a
   Task 4 devesse ter tapado (essa task já tapou `lv2_heap`/`sys_vm`/
   `rsx_local`/`tls`, que eram os buracos genuínos; esta banda nunca foi um
   deles).
2. A Task 5 já tinha confirmado por *facto de código* (não correlação) que
   o EA dominante é disjunto de `SYS_MEM_ALLOC_BASE/END`
   (0x40000000–0x60000000) — não vem de uma alocação legítima do
   `sys_memory.c` que faltasse registar.
3. **Desligar a máscara não revela um caminho alternativo saudável — revela
   um crash.** Se isto fosse um falso positivo do guard (memória
   legitimamente comitada que o guard trata como não-comitada), desligá-lo
   teria de deixar o boot continuar sem incidente. Não continua: o processo
   morre por SIGBUS a poucas linhas do ponto onde o controlo começa a
   engolir `0x91030047` — **qual acesso específico o matou não foi
   determinado** (ver hipóteses alternativas na secção acima, incluindo a
   possibilidade de o próprio gate novo ou um acesso a jusante serem a
   causa); a afirmação sustentada é a posição no log, não a cadeia causal
   exacta.
4. Determinismo entre dois runs independentes (E1 e controlo, mesmos EAs,
   mesmas funções, mesmo último marco) é a assinatura de um bug real e
   reprodutível no alocador guest, não de ruído.

---

## Máscara 2 — `tblsize_guard`

### Quanto engole (E1, in-boot)

`grep -c 'mask=tblsize_guard' f1_e1.log` → **1 linha** (sem cap nem
truncagem — muito abaixo do limiar de 64):

```
[PS3T] 4149520 | 0x4D41494E | _Z13func_0037600CP11ppu_context+0x28C | MEMORY | SWALLOWED | ea=0x00572BB0 len=4 mask=tblsize_guard op=write region=main
```

Idêntico no run de controlo (`ea=0x00572BB0`, mesma função, mesma linha
relativa). **Rótulo: in-boot**, confirmado em 2 runs (mais um terceiro no
run "longo" acidental do E2-b, ver abaixo — sempre a mesma contagem: 1).

**Distinção obrigatória (G5):** o guard protege **6 EAs**
(`0x572BB4`, `0x572BB8`, `0x572BB0`, `0x53FBF0`, `0x53FF4C`, `0x53D6AC`,
`ppu_loader.cpp:1000-1005`), cada um documentado em detalhe no código
(`ppu_loader.cpp:960-990`) como vítima do mesmo padrão de loop
runaway-de-cópia. **Só 1 destes 6 (`0x572BB0`) disparou nesta janela.** Os
outros 5 são **não-exercitados nesta janela** — não "não acontecem". Em
particular, `0x53D6AC` (ponteiro do sistema de shaders usado para construir
a string de combinação `.ps3fx`) é o mais relevante para a Meta corrente do
projecto (pixels de jogo reais) e continua sem nenhuma observação directa,
em nenhum dos runs desta task.

### Top de funções guest atribuídas

Uma única função em todos os runs: `_Z13func_0037600CP11ppu_context+0x28C`.
**Rótulo: in-boot.**

### Distribuição por região

**100% (1/1): `region=main`** — ao contrário do `uncommitted_guard`, este EA
está dentro de memória guest genuinamente válida e comitada
(`VM_MAIN_MEM_BASE`..`+VM_MAIN_MEM_SIZE`). **Rótulo: in-boot.**

### Até onde o boot chega sem a máscara (E2-b, `PS3_MASK_OFF_TBLSIZE=1`)

**Sem crash.** Run autoritativo (com `exec`, ~102 s de parede, morto por
PID, sem órfãos):

| | Controlo | E2-b (`MASK_OFF_TBLSIZE=1`) |
|---|---|---|
| `st620` | 0→1→3→11→0 | **idêntico**: 0→1→3→11→0 |
| WAD R_PermA | completo, `bytes_read=20169344/20169344` (linha 24453) | completo, `bytes_read=20169344/20169344` (linha 26411) |
| `mask=tblsize_guard` | 1 | **0** (esperado — o gate sai antes do `ps3_mask_swallowed_report`) |
| `[FIX] blocked spurious zero at [0x00572BB0]` | aparece, e desta vez **é verdade** (a máscara está ligada, a escrita foi mesmo bloqueada) | **aparece igual, mas MENTE** — confirma o aviso #1 da **mensagem de despacho desta task** (não do `task-6-brief.md`): esta linha ad-hoc corre antes do gate novo, diz "blocked" mesmo com a escrita a passar |
| `mask=uncommitted_guard` | 81 (burst completo) | 0 no run de ~102 s — o kill apanhou o processo ~420 linhas *antes* do burst começar (variância de tempo de parede, a máquina tinha nessa altura um segundo `boot_gow2` de outra sessão a competir por CPU; não é efeito do `tblsize_guard`) |

**Confirmação suplementar (o run "longo" acidental, ~5 min sem supervisão,
ver nota metodológica):** mesmo com o `tblsize_guard` desligado durante toda
essa janela muito mais longa, **não houve crash** e o burst do
`uncommitted_guard` ocorreu (83 linhas, último marco `count=16777216` =
2^24, mesmos EAs/funções). Uso este run como confirmação, e não como
descarte, porque as duas observações que ele sustenta — "não crashou" e "o
burst aconteceu" — são **binárias e robustas a variação de carga da
máquina**: uma janela mais longa só dá mais oportunidade ao crash ou ao
burst de ocorrer, nunca menos; se algo fosse instável só pela ausência do
`tblsize_guard`, 5 minutos sem supervisão era mais, não menos, provável de
o revelar. **O que este run NÃO é**: uma versão "mais longa" do run limpo
de ~102 s autoritativo citado na tabela acima. É uma **execução separada**,
lançada com a mesma configuração mas vítima do erro operacional (ficou
órfã, sem supervisão, por acaso) — a sua duração exacta e os seus
contadores (83 linhas, marco 2^24) pertencem a essa execução distinta e não
devem ser somados nem comparados directamente aos números do run
autoritativo de ~102 s; por isso reporto os dois separadamente em vez de os
fundir numa só linha da tabela. **Rótulo: in-boot** (execução distinta,
válida como observação independente — o jogo correu sem supervisão, não os
meus contadores).

**O que este run não pode responder:** o comentário do código
(`ppu_loader.cpp:987-994`) atribui a este guard consequências específicas
*downstream* se o zero-write passar — dispatch de shaders a degenerar para
zero via `0x53D6AC`, BST nula, dispatch de opcode a abortar. Nenhum destes
sintomas foi observado (nem o oposto) porque **nenhum run desta task saiu
do mesmo ciclo de spin do alocador free-list que também limita o
controlo** — não chegámos ao código downstream que exercitaria essa
consequência, com ou sem a máscara. **Rótulo: não-exercitado.**

### Veredicto — `tblsize_guard`

**Para o único EA exercitado nesta janela (`0x572BB0`): não é bug de
mapeamento; entre "lacuna de seeding do nosso ambiente de recompilação" e
"bug de lift em `func_0037600C`" esta task não discrimina.** Não generalizo
nada disto aos outros 5 EAs que o guard protege — continuam
não-exercitados (ver secção acima).

**O que a minha medição sustenta directamente, sem depender de nenhum
comentário de código:** um evento, `region=main` (endereço válido e
gravável, dentro da memória principal comitada — `vm.h` não tem culpa
aqui, ao contrário do que a Task 4 corrigiu para `lv2_heap`/`sys_vm`/
`rsx_local`), escrito por `func_0037600C+0x28C`, e **sem crash ao desligar
a máscara** durante toda a janela observada (~102 s no run autoritativo,
mais o run acidental de ~5 min — ver confirmação suplementar abaixo).

**O que NÃO é medição minha — é uma leitura herdada do comentário do código
(`ppu_loader.cpp:960-990`), citada mas não reverificada nesta task:** esse
comentário afirma que o valor correcto existe nos bytes crus do ELF e
atribui o zero-write a um loop cujo limite deveria ser semeado por
parâmetros de arranque de processo / tamanhos de relocação que o firmware
SCE real forneceria e que o nosso ambiente não reproduz. **Não reabri os
bytes do ELF nem tracei o loop nesta task** — estou a citar a alegação do
comentário, não a confirmar de novo. Se essa alegação estiver certa, a
máscara compensa uma lacuna de fidelidade do nosso ambiente de
recompilação, não um erro de mapeamento nem uma corrupção "de fábrica".

**Hipótese alternativa que não excluí: erro de lift em `func_0037600C`.**
Este projecto já teve um bug que "parecia do jogo" durante meses e era do
lifter (`func_002550C8`, fallthrough cross-fragment — `CLAUDE.md`, secção
"O megaprojeto vencido") — e a auditoria sistemática desse mesmo padrão no
resto do lift continua pendente (`CLAUDE.md`, "Próximos passos", item 3).
Sem disassembly comparado de `func_0037600C` contra o PPC original, não
posso excluir que o "loop runaway" seja, ele próprio, um artefacto do
lifter (ex.: um bound de loop mal liftado) em vez de um loop genuinamente
fiel a correr sobre uma precondição em falta no nosso ambiente. As duas
hipóteses previam o mesmo sintoma observável (zero-write nesse EA); esta
task não tem dado que separe uma da outra.

- Consequência prática do que É medido: desligar esta máscara **não
  crasha** (ao contrário do `uncommitted_guard`), porque a memória está
  mapeada e gravável. A consequência a jusante que o comentário do código
  descreve (shaders degradados via `0x53D6AC`, dispatch a abortar) fica
  **não-exercitada**: nenhum run desta task passou do mesmo ponto de spin
  do alocador que também trava o controlo, por isso não a observei, não a
  confirmei e não a refutei.
- Esta secção inteira aplica-se só ao EA exercitado (`0x572BB0`); os
  outros 5 (`0x572BB4`, `0x572BB8`, `0x53FBF0`, `0x53FF4C`, `0x53D6AC`)
  continuam **não-exercitados** — não tenho base para estender nem o
  veredicto nem qualquer uma das duas hipóteses a eles.

---

## Achados transversais

- **`rsx_local` continua não-exercitado em trace real** em todos os 5 runs
  desta task (0 ocorrências de `region=rsx_local` em qualquer log,
  incluindo o run "longo" de ~5 min). Não é uma refutação da lógica (essa
  está provada offline-unit, Task 4) — é simplesmente que nenhum destes
  boots chegou a tocar memória nessa banda antes de ficar preso no ciclo do
  alocador free-list.
- **Determinismo do bug do `uncommitted_guard`**: E1 e o controlo, dois
  processos separados, reproduziram os mesmos 64 EAs/funções na mesma
  ordem relativa e o mesmo último marco de truncagem (`count=4194304`) —
  não há aleatoriedade visível nesta janela de boot.
- **`vm commit (sys_memory window) failed`**: 0 ocorrências em 5 runs, 0
  repetições necessárias.
- **Ambiente partilhado**: um segundo `boot_gow2` de outra sessão (PID
  84355) apareceu durante o E2-b — não foi tocado, mas explica a variação
  de velocidade de parede entre runs (o mesmo motivo já documentado na
  Task 2).

### `primeiro_erro` por run (`tools/trace_parse.py`, pedido pelo brief)

| Run | `primeiro_erro` |
|---|---|
| E1 | `MEMORY\|OOB`, `ea=0xFB788082`, `_Z13func_00263554P11ppu_context+0x1A8` |
| controlo | idêntico: `ea=0xFB788082`, `_Z13func_00263554P11ppu_context+0x1A8` |
| E2-a | `null` — o processo crashou antes de o parser encontrar este evento no seu próprio log |
| E2-b | `null` — o kill apanhou o processo antes de o parser encontrar este evento (mesma janela curta descrita acima) |

Isto é relevante por si, não só como metadado do parser: `0xFB788082` é um
**OOB** (`a+n > ppu_vm_size`), e os doze sítios que emitem `MEMORY|OOB`
retornam **antes** de `uncommitted_guard` sequer correr — não é "estar numa
banda diferente da que o guard cobre" (`uncommitted_guard` não filtra por
banda para decidir se intercepta; só olha se há uma range registada que
cubra o endereço). É um ponteiro selvagem que **nenhuma das
duas máscaras desta task apanha** porque o guard de OOB corta o acesso
primeiro, sinalizado pelo subsistema canónico
`MEMORY|OOB` da Fase 0 (não pelo `MEMORY|SWALLOWED` desta fase), na mesma
janela de boot (dentro do burst do alocador free-list, tanto em E1 como no
controlo). A função atribuída, `func_00263554`, **também aparece no top-10
de `uncommitted_guard`** acima (`+0xD4`, offset diferente do `+0x1A8` deste
evento, mas a mesma função base) — candidato a olhar se algum dia se fizer
a auditoria sistemática de fallthrough cross-fragment que o `CLAUDE.md`
deixa pendente. Não investiguei mais fundo: está fora do escopo desta task.
**Rótulo: in-boot** (E1/controlo), **não-exercitado** (E2-a/E2-b, janela
curta demais).

## Resumo por máscara

| | `uncommitted_guard` | `tblsize_guard` |
|---|---|---|
| Volume (E1, limite inferior) | ≥4.194.304 eventos | 1 evento (do único EA exercitado; 5 outros não-exercitados) |
| Região | 100% `unmapped` (fora de qualquer mapa válido) | 100% `main` (válida) |
| Sem a máscara | **SIGBUS** (n=1), morre sozinho, mesma posição de log do controlo — mecanismo causal não confirmado por backtrace | **sem crash** durante toda a janela observada (~102 s + ~5 min); a janela limpa de ~102 s termina ~420 linhas *antes* do burst do outro guard começar, por isso cobre um trecho mais curto do boot que o controlo, não um trecho "idêntico" |
| Natureza | banda `0x8-0x9xxx` documentada no próprio código como assinatura de ponteiro-lixo de alocador; identidade do chamador dominante (`func_000B951C`) com o "alocador" é hipótese, não confirmada | para `0x572BB0`: não é bug de mapeamento (`region=main` válido); entre lacuna de seeding do ambiente de recomp (herdada do comentário do código, não reverificada) e bug de lift em `func_0037600C` (não excluído) — não discriminado |
| Veredicto | esconde corrupção real — vital, não remover sem corrigir o alocador primeiro | não é erro de `vm.h`; entre lacuna de fidelidade do recomp e bug de lift, não discriminado; consequência a jusante não-exercitada; generalização aos outros 5 EAs não sustentada |
