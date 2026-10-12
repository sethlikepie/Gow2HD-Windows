# G4 — o gate real do open de `R_LglScA`/`R_PermA` (e por que nunca dispara no Mac)

Task 3, sub-faixa 3-G4 (`ps3recomp/.superpowers/sdd/task-3-brief.md`). Método: RE
estática do lift (`recomp_macos_v2/ppu_recomp_0NN.cpp`, gitignored, sempre
`grep -n` + `sed -n`/Read com range) **mais** 4 boots in-boot medidos (armados,
`PS3_TRACE_MOVIEOBJ=1`/`PS3_TRACE_SMPD=1`/`PS3_DUMP_IMPORTS=1`), matados sempre
por PID (`TERM` depois `-9`, `wait`, `pgrep` a confirmar 0 no fim de cada um).
Duas sondas diagnósticas novas, read-only, gated, adicionadas em `ps3recomp`
(runtime, não-lift) para decidir a questão — ver secção 6.

---

## TL;DR — veredito

**G4 refinado, não confirmado como estava formulado.** O "lead forte" herdado
(`func_0045B2A8`/`obj+0x720` = "sessão de decode vdec fria") está **refutado**:
`obj+0x720` é o handle do **snd_stream de ÁUDIO** (não vídeo — confirmado por
RE cruzada com `2026-07-20-macos-intro-audio-open-wall.md`), e a leitura da
condição de avanço estava **invertida** (secção 3): um handle **não resolvido**
faz `func_0045B2A8` devolver **-1**, e -1 **avança** a FSM (só o **0 literal**
pára). Isso não é o que bloqueia o Open.

**O bloqueio real, medido in-boot (secção 5):** `cellVdecOpenEx`/`cellVdecStartSeq`
têm **exactamente 1 site de chamada estático cada, em todo o lift de 31 chunks**
— dentro do handler do **estado 4** da FSM (`func_002C069C`), condicionado a
`vm_read8(obj+0x744) == 0`. O handler do **estado 3** (`func_002C05F8`) faz o
**mesmo teste primeiro**: se `+0x744 != 0`, salta directamente para o estado 5
**sem nunca entrar no estado 4** — nunca chamando Open. **O nosso próprio
mecanismo de arm de EOS (`movie_eos_arm.c`, usado por todas as sessões
anteriores para destravar `st620` 1→3) marca `+0x744` como "já terminado"
exactamente no momento em que os estados 3/4 fazem essa pergunta — e eles
interpretam "já terminado" como "não há nada para abrir", não como "o vídeo
já acabou de tocar".** Confirmado por trace `[EOSGATE]` em boot armado
(secção 5.2): o site do estado 3 dispara `val=1 branch=adv` no primeiro tick
pós-arm; o site do estado 4 nunca dispara nenhuma vez.

**Resultado:** 0 WADs abertos (0/4 boots medidos nesta task; consistente com
os 0/6 herdados). **Não** implementei o fix (é preciso editar
`movie_eos_arm.c`, ficheiro de outra sessão activa — ver secção 7) e não
consegui verificar em boot um fix hipotético porque, a meio da task, uma
sessão concorrente sobrescreveu o `boot_gow2`/`libps3recomp_runtime.a`
partilhados com um build **que ela própria documentou como regressão**
("st620 ficou 0, OOB stack" — `ps3recomp/CLAUDE.md`, secção "Alternativa mais
segura que merge total") — ver secção 8. Isto não é reclass "vdec frio
irrecuperável" (essa premissa caiu); é "causa raiz nomeada com prova, fix
apontado mas bloqueado por ambiente partilhado instável no momento desta
sessão".

---

## 1. O achado principal — onde `cellVdecOpenEx`/`StartSeq` são chamados

`PS3_DUMP_IMPORTS=1` (gate pré-existente em `ppu_imports.cpp`, ampliado nesta
task para também imprimir `tramp`, o endereço-alvo real que o `bl` do jogo usa
— ver secção 6) dá os 8 imports do módulo `libvdec` desta build:

| NID | símbolo (via `gen/ppu_hle_nids.cpp`) | `tramp` (EA guest chamado pelo jogo) |
|---|---|---|
| `0x0053E2D8` | `cellVdecOpenEx` | `0x004B9398` |
| `0xC757C2AA` | `cellVdecStartSeq` | `0x004B9458` |
| `0xC982A84A` | `cellVdecQueryAttrEx` | `0x004B9478` |
| `0xB6BBCD5D` (`cellVdecOpen`, não-Ex) | — | **não está na lista de imports desta build** |

O título **não importa `cellVdecOpen`** — só `cellVdecOpenEx` (o HLE mapeia
ambos para o mesmo `cellVdecOpen()` em `libs/codec/cellVdec.c`, então o log
`[cellVdec] Open ...` cobre os dois casos por igual).

Grep de `func_004B9398(` e `func_004B9458(` nos 31 chunks (`ppu_recomp_000.cpp`
… `ppu_recomp_030.cpp`, ~18M linhas):

```
recomp_macos_v2/ppu_recomp_001.cpp:36727:  func_004B9398(ctx); DRAIN_TRAMPOLINE(ctx);   // cellVdecOpenEx
recomp_macos_v2/ppu_recomp_001.cpp:36730:  func_004B9458(ctx); DRAIN_TRAMPOLINE(ctx);   // cellVdecStartSeq
```

**Um único site de chamada cada, em todo o binário do jogo.** Ambos dentro de
`func_002C069C` (`ppu_recomp_001.cpp:36675`), que é o handler do **estado 4**
da FSM do movie player (jump table do pump, confirmada em
`2026-07-20-st620-3to0-static.md`: `st620==4 -> 0x2C069C`).

---

## 2. `func_002C069C` (estado 4) — o corpo completo do gate

```c
// ppu_recomp_001.cpp:36675
void func_002C069C(ppu_context* ctx) {
        ctx->gpr[0] = vm_read8(ctx->gpr[30] + 0x744);          // :36676 — relê o byte de EOS
        ...
        if ((!((ctx->cr >> 0) & 2))) {                          // :36686 — SE +0x744 != 0
            g_trampoline_fn = (void(*)(void*))func_002C060C; return;   //   -> salta p/ st620=5, NÃO abre
        }
        // ... monta os argumentos (type@obj+0x748, res, cb, handle@obj+0x668) ...
        func_004B9398(ctx); DRAIN_TRAMPOLINE(ctx);              // :36727 — cellVdecOpenEx
        ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
        ctx->gpr[3] = vm_read32(ctx->gpr[30] + 0x668);          // handle devolvido por OpenEx
        func_004B9458(ctx); DRAIN_TRAMPOLINE(ctx);              // :36730 — cellVdecStartSeq
        ctx->gpr[2] = vm_read64(ctx->gpr[1] + 0x28);
        ctx->gpr[0] = 5; vm_write32(ctx->gpr[30] + 0x620, ctx->gpr[0]);  // st620=5, incondicional
        { g_trampoline_fn = (void(*)(void*))func_002C0760; return; }    // :36734 — avança p/ estado 5
}
```

Traduzido: **"se o filme já está marcado como terminado, não há nada para
decodificar — salta a abertura e vai para o estado 5. Senão, abre o
decodificador de vídeo, arranca a sequência, e avança."** Isto é código são —
é exactamente o que se espera de um player real (não reabrir um decoder para
um clip já consumido). O bug não está aqui.

O handler do **estado 3** (`func_002C05F8`, `ppu_recomp_001.cpp:36610`) faz o
**mesmíssimo teste, primeiro**:

```c
// ppu_recomp_001.cpp:36610
void func_002C05F8(ppu_context* ctx) {
        ctx->gpr[0] = vm_read8(ctx->gpr[30] + 0x744);           // :36611
        ...
        if (((ctx->cr >> 0) & 2)) {                              // :36629 — SE +0x744 == 0
            g_trampoline_fn = (void(*)(void*))func_002C0688; return;  //  -> cadeia normal (secção 3)
        }
        // +0x744 != 0: "já terminado" — nem passa pelo estado 4
        ctx->gpr[0]=4; vm_write32(obj+0x620, gpr0);
        ctx->gpr[0]=5; vm_write32(obj+0x620, gpr0);              // st620 vira 5 directo (4 é transitório, nunca despachado)
        { g_trampoline_fn = (void(*)(void*))func_002C060C; return; }  // :36634
}
```

Ou seja: **há DOIS pontos (estado 3 e estado 4) que tratam `+0x744 != 0` como
"nada a abrir"** — e o estado 3 é o primeiro a correr.

---

## 3. Correcção ao "lead forte" herdado — `obj+0x720` não é sessão de vídeo

Quando `+0x744 == 0` (o caso normal, logo após o `Play` limpar os bytes de EOS
— confirmado em `2026-07-20-macos-intro-audio-open-wall.md` secção (a)), o
estado 3 cai em `func_002C0688` (`ppu_recomp_002.cpp:224843`):

```c
void func_002C0688(ppu_context* ctx) {
        ctx->gpr[0] = vm_read8(ctx->gpr[30] + 0x746);
        if (EQ) { -> func_002C0FA0; return; }          // +0x746==0
        st620 = 4; { -> func_002C0694; return; }        // +0x746!=0 -> avança directo
}
void func_002C0694(ppu_context* ctx) {                  // "avançar para estado 4"
        st620 = 4; { -> func_002C069C; return; }
}
void func_002C0FA0(ppu_context* ctx) {                   // ppu_recomp_002.cpp:224858
        ctx->gpr[3] = vm_read32(ctx->gpr[30] + 0x720);
        func_0045B2A8(ctx); DRAIN_TRAMPOLINE(ctx);        // gpr3 = resultado
        if (EQ, i.e. gpr3==0) { -> func_002C0614; return; }   // PARK só se == 0 literal
        { -> func_002C0694; return; }                    // qualquer outro valor (inclui -1!) -> AVANÇA
}
```

E `func_0045B2A8` (`ppu_recomp_001.cpp:426000` — **não 425951** como o brief
citava; linha antiga já não bate, lift foi regerado; sempre re-verificar):

```c
void func_0045B2A8(ppu_context* ctx) {
        ...
        func_004479E8(ctx); DRAIN_TRAMPOLINE(ctx);   // resolve handle geracional (gpr3=obj+0x720 na entrada)
        gpr0 = -1;                                    // default
        if (gpr3_resolvido == 0) goto loc_0045B2CC;    // resolve FALHOU -> mantém -1
        gpr0 = vm_read32(gpr3_resolvido + 0x1B8);       // resolve OK -> lê o campo real
loc_0045B2CC:
        gpr3 = gpr0; return;   // valor devolvido
}
```

**Isto é o oposto do que os documentos anteriores (`2026-07-20-st620-3to0-static.md`,
`2026-07-20-macos-post-st3-smpd-wad.md`, e o brief desta task) assumiam.** A
leitura antiga era "obj+0x720 precisa de uma sessão viva com `+0x1B8!=0`;
sem sessão, `func_0045B2A8` devolve 0/-1, fica parado". A leitura correcta
(confirmada por leitura directa do C gerado, não por prosa de nota antiga) é:

- **Handle não resolve (obj+0x720 vazio/inválido) → devolve literal `-1` → `-1 != 0` → `func_002C0FA0` AVANÇA.**
- **Handle resolve E `*(sessão+0x1B8) == 0` → devolve literal `0` → PARA.**
- Só este segundo caso pára. Um "não há sessão nenhuma" **não** é o que
  bloqueia — pelo contrário, avançaria de imediato.

E o que é `obj+0x720`? Cruzando com a RE já feita (não desta task) em
`2026-07-20-macos-intro-audio-open-wall.md` secção (a), o `Play`
(`func_002C00DC`) chama `func_0045E230` ("**snd_stream OPEN (returns into
obj+0x720)**") — é o handle do **áudio** (`.wav` da SmLogo), não do vídeo. O
handle de vídeo que `cellVdecOpenEx` devolve vai para **`obj+0x668`**
(confirmado nesta task, secção 2 — é o `r6`/4º argumento passado a
`func_004B9398`). São dois handles completamente distintos.

**Conclusão da secção:** o gate do estado 3 (`func_0045B2A8(obj+0x720)`) está
a verificar se o **subsistema de áudio** atingiu algum marco (não zero em
`+0x1B8`) — plausivelmente "áudio pronto/terminado" — **antes** de abrir o
decodificador de vídeo. Como o `.wav` abre com sucesso no Mac (já provado,
`543ab35`, "WAV_SURVIVABLE"), `obj+0x720` deve resolver para uma sessão real,
e por isso o campo genuinamente começa em 0 e o estado 3 fica à espera dele
progredir — **não** por falta de handle.

---

## 4. Porque é que isto ainda faz o `st620` avançar 1→3→(cascata)→0 tal como medido antes

Nada disto contradiz as notas anteriores sobre o `st620` chegar a 3 (sticky
FIOS, Task 8) nem sobre a cascata 3→11→Stop quando o hook dispara (Task 1 do
plano EOS). O que esta task acrescenta é **o que acontece DENTRO dessa
cascata, no exacto tick em que o hook arma**: ela passa pelo estado 4 **sem
nunca o executar de facto** (state 3 já desvia para 5 antes de o pump alguma
vez despachar para `func_002C069C`). Isso é o que faltava explicar — porque é
que a cascata "3→…→11" nunca deixa rasto de `[cellVdec] Open`.

---

## 5. Prova in-boot (antes da contaminação de ambiente, secção 8)

### 5.1 — `cellVdecOpen`/`StartSeq` continuam 0 com o recipe natural actual

Boot armado, recipe canónico (`env_gow2.sh`) + `PS3_MOVIE_DONE_MS=auto` +
`PS3_NO_RSX=1` + `PS3_TRACE_MOVIEOBJ=1` + `PS3_VDEC_STATS=1`, 45–60 s, PID-kill:

```
[cellVdec] QueryAttrEx codec=0 -> memSize=4MB     (2 linhas = 1 chamada)
[cellVdec] Open ...                                0 ocorrências
[cellVdec] StartSeq ...                            0 ocorrências
FORCE SEQDONE                                       0 ocorrências (StartSeq nunca latch o watchdog)
[movieio]/[fs] open com "R_LglScA"/"R_PermA"/"wad_ps3" (case-insensitive)   0/63291 linhas
```

Confirma o "cellVdecQueryAttrEx chamado, Open nunca" do brief — **ainda
verdadeiro no código actual**, mas por uma razão diferente da suposta (secção 3).

### 5.2 — o trace `[EOSGATE]` apanha o momento exacto do desvio

Boot armado com `PS3_TRACE_SMPD=1` adicional (sondas pré-existentes de
`patch_smpd_probe.py`, sites já instrumentados em `func_002C05F8`/estado 3 e
`func_002C0788`/estado 10 — **não** instrumentado em `func_002C069C`/estado 4
por esta task ainda, ver secção 9):

```
[MOVIEFSM] st620 0 -> 1
[EOSGATE] site=002C07D8 val=0 branch=wait      (estado 1, ainda fresco)
[EOSGATE] site=002C05C8 val=0 branch=wait
[MOVIEFSM] st620 1 -> 3                          (via sticky FIOS, Task 8 — não via +0x744)
[MOVIEFSM] st620 3 -> 3   (parado, ~15s, duração ≈ do .wav SmLogo_v2.wav, 2 829 884 B / 192 512 B/s ≈ 14,7 s)
[MOVIEEOS] time-based done (st620=3) -> arming EOS read-hook at 0x00869F1C ([obj+0x744])
[MOVIEEOS] read-hook HIT [0x00869F1C]->1 ra=0x10183104c
[EOSGATE] site=002C05F8 obj=0x008697D8 ea=0x00869F1C val=1 branch=adv    <-- ESTADO 3 vê +0x744=1, desvia
[MOVIEEOS] read-hook HIT [0x00869F1C]->1 ra=0x101831fb8
[EOSGATE] site=002C0788 obj=0x008697D8 ea=0x00869F1C val=1 branch=adv    <-- ESTADO 10 (cascata continua)
[MOVIEEOS] read-hook HIT [0x00869F1C]->1 ra=0x1018329d0
[MOVIEFSM] st620 3 -> 0     (Stop/teardown fechou o ciclo; nenhum "site=002C069C" apareceu)
```

**Zero linhas `site=002C069C`** em toda a corrida — a prova directa (não só
inferida do grafo estático) de que o estado 4 nunca é despachado quando o
hook dispara enquanto `st620==3`.

---

## 6. Sondas adicionadas nesta task (read-only, gated, sem escrita de estado guest)

Ambas em `ps3recomp` (runtime, não-lift; não precisam de `patch_*.py`), sem
gate novo — reaproveitam o `PS3_DUMP_IMPORTS`/log incondicional já existente:

1. `libs/codec/cellVdec.c` — `[cellVdec] QueryAttrEx`/`Open` passam a incluir
   `lr=` (guest LR no momento da chamada) + `guest_stack_hint=` (scan de
   fallback já existente em `ppu_hle.cpp`/`g_last_hle_caller_lr`/
   `g_last_hle_caller_guest[4]`). **Resultado: `lr=0x0`, hints todos `0x0`** —
   este caminho específico não deu o caller (LR zera em cadeias de tail-call
   via `DRAIN_TRAMPOLINE`, e o scan POSIX do stack guest é só 0x40 bytes, curto
   demais). Não foi o que resolveu a questão — o grep estático de `tramp` (item
   2) foi o que funcionou; mantenho esta sonda por ser honesta e barata, não
   por ter sido decisiva.
2. `runtime/ppu/ppu_imports.cpp` — a linha `[imp-nid]` (gate `PS3_DUMP_IMPORTS`)
   passa a imprimir também `tramp=0x%08X` (o EA que o `bl` do jogo chama
   directamente), não só `slot=` (a célula de ponteiro que o tramp lê). Foi
   **esta** sonda que deu os endereços da secção 1.

Ambas foram apanhadas por um `git stash -u` de outra sessão (integração de
upstream a meio desta task — secção 8) e acabaram commitadas por essa sessão
em `ps3recomp` commit `2883a19` ("docs+diag: plano pos-st3/SMPD/WAD, INDEX, e
probes G4 de caller vdec"). Não as re-commitei eu — já estão em histórico,
com o conteúdo exacto que escrevi. Confirmado via `git show --stat 2883a19` e
`grep` dos marcadores (`g_last_hle_caller_lr`, `tramp=0x%08X`) no ficheiro
actual.

---

## 7. Fix honesto identificado, não implementado

**A hipótese com mais evidência:** o mecanismo de arm de EOS
(`movie_eos_arm.c`, produtor `PS3_MOVIE_DONE_MS=auto`) dispara com base em
**tempo de relógio** (duração real do `.wav`), sem olhar para o estado da
FSM. Isso está correcto para o que ele foi construído a resolver (Task 3 do
plano `macos-movie-eos-fsm`: sair do `st620=1`) mas **colide** com o
significado alternativo de `+0x744` nos estados 3/4 ("nada para abrir"). Um
fix fiel, sem forjar nada, seria: **só armar o hook depois de `st620` já ter
passado o estado 4** (ex.: gate adicional `st620>=5` na condição de arm, ao
lado do gate de tempo já existente) — assim os estados 3/4 continuam a ver
`+0x744==0` (verdade, o filme não acabou) e o `cellVdecOpenEx`/`StartSeq`
correm pela via natural do jogo; o hook só entraria em jogo para o que quer
que seja o gate de "vídeo realmente terminou" mais à frente (estado 5+, fora
do escopo desta task — não segui essa cadeia).

**Não implementei este fix** porque:

1. Exige editar `movie_eos_arm.c` — ficheiro listado explicitamente no brief
   como pertencente a outra sessão em curso (estava `M` no início desta task;
   passou a commitado, `04d651e`, a meio da minha sessão, como parte de um
   feature maior de PERF/LIFT_OPT dessa outra sessão). Editar por cima
   arriscava colidir com trabalho activo.
2. Mesmo que editasse, **não consigo verificar em boot agora** — secção 8.

**Não sei se este fix, sozinho, chega a abrir o WAD.** Mesmo que os estados
3→4 avancem naturalmente e `cellVdecOpenEx`/`StartSeq` corram, falta saber se
o WAD-loader (disparado por algum estado mais à frente, ainda não localizado
nesta task — não é o mesmo código que a FSM do movie player, dado que
`R_LglScA`/`R_PermA` nunca aparecem como string no lift, só via `cellFsOpen`
com o nome resolvido pelo `movie_io` do host) chega a ser invocado só por
`st620` avançar, ou se depende ainda de outro sinal a jusante (ex.: o próprio
SEQDONE do vídeo, que por sua vez precisaria do `PS3_VDEC_FORCE_SEQDONE_MS`
já portado para POSIX em `049fdf9` — esse watchdog HONESTAMENTE chama o
callback OPD do próprio guest, não escreve estado; seria a próxima peça a
testar, IF o estado 4 passar a correr).

---

## 8. Porque parei os boots ao vivo — contaminação de ambiente partilhado

A meio desta task, outra sessão fez `git stash push -u` + `git merge
upstream/master` (v0.7.0 "First Draw") em `ps3recomp`, seguido de correcções
de portabilidade e `git stash pop` — tudo dentro da MESMA árvore de trabalho
que eu estava a usar (confirmado via `git reflog`/`git log` em `ps3recomp`:
`25b0110` merge, `28367b2` fix portabilidade, `2883a19` stash-pop+commit). A
mesma sessão **rebuildou** `ps3recomp/build-macos/libps3recomp_runtime.a`
(mtime 09:41) e o `gow2-recomp/boot_gow2` partilhado (mtime 09:43) **por
cima** do binário que eu vinha a usar (build meu de 09:31, o que produziu toda
a secção 5).

O binário novo está com uma regressão que a PRÓPRIA sessão já documentou no
`ps3recomp/CLAUDE.md` (secção "Alternativa mais segura que merge total"):
*"como no merge v0.7 de 2026-07-21: st620 ficou 0, OOB stack"*. Reproduzi
isto directamente: corri o MESMO recipe que tinha dado a secção 5 (que
funcionava) contra o binário novo e `st620` fica preso em 0 a corrida
inteira, sem excepção — não é nondeterminismo nem efeito de qualquer coisa
que eu tenha mudado (não toquei `movie_eos_arm.c`, `ppu_loader.cpp`, nem
nada do caminho da FSM).

**Decisão:** parei de tentar boots ao vivo depois de confirmar a regressão
(2 corridas, ambas presas em `st620=0`, ambas limpas por PID — `pgrep -f
'boot_gow2 EBOOT'` = 0 no fim de cada uma). Insistir contra um binário
partilhado que a outra sessão está activamente a corrigir arriscava gastar
tempo a diagnosticar o problema ERRADO (o deles, não o meu) ou pisar o
trabalho deles. As secções 1–5 (achado principal, correcção do lead, e a
prova `[EOSGATE]`) foram todas obtidas **antes** desta contaminação, com o
binário de 09:31 — válidas e reprodutíveis independentemente disto assim que
o `boot_gow2` partilhado voltar a um estado São (a RE estática, secções 1–3,
é 100% independente de qualquer binário — é leitura de texto do lift).

---

## 9. O que não resolvi

**Se o gate do estado 3 (`func_0045B2A8`/`obj+0x720`, agora identificado como
sessão de ÁUDIO) alguma vez resolve sozinho, sem qualquer hook do host.** A
experiência discriminadora óbvia (`PS3_MOVIE_EOS=0`, deixar correr 45-50s sem
armar nada, ver se `st620` sai de 3 por si) foi tentada duas vezes (`run5`,
`run6`) mas caiu exactamente na janela da contaminação da secção 8 — os dois
resultados mostram `st620` preso em **0** (nem chega a 1), inconsistente com
todas as corridas anteriores desta sessão e com o baseline histórico, então
não os conto como medição válida desta questão (são medição válida da
regressão da outra sessão, não da minha pergunta).

Isto importa porque decide se o fix da secção 7 basta sozinho: se
`+0x1B8` do handle de áudio progride por si via o `cellAudio` HLE real (que
existe, `libs/audio/cellAudio.c`, 7 funções resolvidas nesta build) mais o
polling que o próprio jogo já faz, então "só atrasar o arm para depois do
estado 4" é suficiente. Se `+0x1B8` só é escrito por um caminho que a nossa
HLE de áudio não completa (ex.: um sub-evento específico do `snd_stream` que
não implementámos), o estado 3 ficaria parado para sempre sem o hook, e o
mesmo fix da secção 7 teria de incluir também um produtor honesto para ESSE
campo (não escrevê-lo directo — chamar o que quer que o jogo espere, no
mesmo espírito do watchdog SEQDONE do vídeo). Não segui `func_004479E8` /
a tabela geracional (`TOC-0x394`, stride `0x1E4`) nem o resto do subsistema
`cellAudio`/`snd_stream` para responder isto — fica para quem continuar.

---

## 10. Regras honradas

- Nenhum `vm_write` de host a `st620`, `obj+0x744`, `obj+0x720`,
  `obj+0x668`, ou qualquer campo de sessão. Todas as sondas (secção 6) só
  leem e imprimem valores já computados pelo próprio guest/HLE existente.
- Nenhum fix aplicado — só diagnóstico. Não tentei "destravar" nada por
  escrita directa em nenhum momento, mesmo depois de identificar o mecanismo
  exacto.
- `M3` (forge-trap: `PS3_MOVIE_EOS=1` sem produtor ⇒ zero arm) não foi
  re-executado nesta task (não mudei nada que o pudesse afectar — as duas
  sondas são só `fprintf` adicionais numa linha já incondicional/já gated
  por `PS3_DUMP_IMPORTS`), mas também não foi verificado de novo por falta
  de um binário são no fim da sessão (secção 8). Recomendo à próxima sessão
  reconfirmar M3 assim que o `boot_gow2` partilhado estabilizar.
- Todos os boots mortos por PID (`TERM` depois `-9`, `wait`, sem `pkill`
  genérico). `pgrep -f 'boot_gow2 EBOOT'` = 0 confirmado depois de cada uma
  das 6 corridas desta task (nenhum órfão deixado).
- Não toquei `build_macos.sh`, `movie_eos_arm.c`, `smoke_perf_macos.sh`,
  `boot_gow2_O1` (ficheiros de outra sessão) nem `ppu_loader.cpp` do
  `ps3recomp` (idem — tinha WIP de PERF/fairness de outra sessão quando
  cheguei).
- Linhas de nota antiga (`2026-07-20-st620-3to0-static.md`, linha
  425951 para `func_0045B2A8`) foram re-verificadas, não assumidas — o lift
  já tinha sido regerado e a linha real é 426000. Confirma a própria regra
  do projecto ("nunca confiar em linha de plano antigo").

---

## Ficheiros/EAs citados (para quem continuar)

| O quê | Onde |
|---|---|
| Estado 3 (gate `+0x744`, 1ª pergunta) | `recomp_macos_v2/ppu_recomp_001.cpp:36610` (`func_002C05F8`), branch :36629 |
| Estado 4 (Open+StartSeq) | `recomp_macos_v2/ppu_recomp_001.cpp:36675` (`func_002C069C`), branch :36686, calls :36727/:36730 |
| Cadeia `+0x746`/áudio | `recomp_macos_v2/ppu_recomp_002.cpp:224843` (`func_002C0688`), `:224852` (`func_002C0694`), `:224858` (`func_002C0FA0`) |
| Gate de áudio (mal identificado antes como vdec) | `recomp_macos_v2/ppu_recomp_001.cpp:426000` (`func_0045B2A8`) |
| Handle de vídeo real (OpenEx output) | `obj+0x668` (não `obj+0x720`) |
| Handle de áudio (snd_stream, é o `obj+0x720`) | escrito por `func_0045E230`, doc `2026-07-20-macos-intro-audio-open-wall.md` §(a) |
| Trampolins do import `libvdec` | OpenEx=`0x004B9398`, StartSeq=`0x004B9458`, QueryAttrEx=`0x004B9478` (via `PS3_DUMP_IMPORTS=1`) |
| Sondas novas (read-only) | `ps3recomp` commit `2883a19` — `libs/codec/cellVdec.c`, `runtime/ppu/ppu_imports.cpp` |
| Regressão de ambiente (não-minha) | `ps3recomp/CLAUDE.md` §"Alternativa mais segura que merge total"; commits `25b0110`/`28367b2`/`2883a19` |

---

## 11. Reconfirmação RED em binário recuperado — 2026-07-21 (Task 1, plano de bring-up macOS/arm64)

Medição pura (sem alteração de código), Task 1 do plano de bring-up
macOS/arm64 (`gow2-recomp/.superpowers/sdd/task-1-brief.md`). Entre a
secção 8 (contaminação de ambiente pelo merge v0.7 concorrente) e agora,
outra sessão **recuperou** o motor (`ps3recomp` commit `11a1c3c`); esta
task reconfirma formalmente que a parede da secção 5.2 continua verdadeira
no binário recuperado, antes de qualquer fix (Task 1 é RED baseline; o fix
fica para task posterior). `boot_gow2` usado: rebuild de 21/jul 10:20
contra o motor recuperado, árvore limpa, sem tocar `movie_eos_arm.c`.
Ambas as corridas mortas por PID (`TERM`→`-9`→`wait`); `pgrep -f
boot_gow2 | wc -l` = 0 confirmado após cada uma (nenhum órfão; a caixa
usa o `pgrep` BSD do macOS, que não tem `-c`).

**Passo 1 — M0 (sem arm), `/tmp/vdec_m0.log`, 30s:**

| métrica | valor |
|---|---|
| st620 max | 3 (0→1→3→3 sticky) |
| cellVdec Open | 0 |
| StartSeq | 0 |
| wad (R_LglScA/R_PermA) | 0 |
| órfãos pós-kill | 0 |

GREEN confirmado (`st620≥3` — motor efectivamente recuperado; `Open=0` é
esperado em M0, não é regressão).

**Passo 2 — RED, arm cedo (`PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=4000
PS3_VDEC_FORCE_SEQDONE_MS=8000`), `/tmp/vdec_red.log`, 45s:**

| métrica | valor | esperado |
|---|---|---|
| arm | 1 | ≥1 |
| hit | 3 | ≥1 |
| open | 0 | ==0 |
| start | 0 | ==0 |
| force | 0 | ==0 |
| st4 (`site=002C069C`) | 0 | ==0 |
| wad | 0 | ==0 |

RED confirmado, 7/7 métricas batem com o esperado. Narrativa igual à
secção 5.2: hook arma em `st620=3` (`0x00869F1C`), 2 hits desviam os
estados 3 e 10 (`site=002C05F8`, `site=002C0788`) para a cascata
`st620 3→0`, e `site=002C069C` (estado 4, onde vivem
`cellVdecOpenEx`/`StartSeq`) nunca dispara — 0 WADs. Confirma que a
parede não era artefacto do binário regredido da secção 8: persiste,
idêntica, no motor recuperado. Fix continua o da secção 7 (gate
`st620>=5` adicional no arm), não aplicado aqui (fora de escopo desta
task).

---

## 12. Task 3 (prova pós-fix) + Task 3b (classificação) — 2026-07-21

Plano `gow2-recomp/.superpowers/sdd/task-3-brief.md`. Task 2 (commit
`e5e1495`, `movie_eos_arm.c`) implementou exactamente o fix apontado na
secção 7: o arm do read-hook de EOS passou a exigir `st620>=5`
(`MOVIE_STATE_POST_OPEN`), além das três condições originais (env,
one-shot, produtor de done). Esta task mede se isso, sozinho, chega para
o estado 4 (`cellVdecOpenEx`/`StartSeq`) despachar.

### 12.1 — Task 3 Step 2: RED, mesmo com o gate novo já aplicado

Rebuild obrigatório primeiro (Task 2 só tinha mudado `movie_eos_arm.c`,
`boot_gow2` continuava a apontar para o binário de antes):
`./build_macos.sh` — 0 erros, `boot_gow2` relincado 21/jul 11:14.

Recipe canónico do brief (`env_gow2.sh` + `PS3_MOVIE_EOS=1
PS3_MOVIE_DONE_MS=auto PS3_VDEC_ASYNC=1`, SEM `FORCE_SEQDONE_MS`), 60 s,
`/tmp/vdec_open.log`, morto por PID:

| métrica | valor | esperado GREEN |
|---|---|---|
| `open` (`[cellVdec] Open`) | 0 | ≥1 |
| `start` (`StartSeq`) | 0 | ≥1 |
| `st4` (`site=002C069C`) | 0 | — |
| `st3_adv` (`site=002C05F8.*val=1`) | 0 | — |

**RED.** `st620` fica preso em `3` a corrida inteira (0→1→3→3…3, nunca
sai). Evidência de que o gate da Task 2 está a funcionar **como
desenhado** (não é regressão dele):

```
[MOVIEDONE] PS3_MOVIE_DONE_MS=auto -> duracao REAL do .wav = 14738 ms
[MOVIEDONE] produtor time-based LIGADO: intervalo=14738 ms. ...
[MOVIEDONE] done time-based (NAO e' EOF real): 14894 ms desde st620 activo >= 14738 ms, player parado em st620=3 -> sinal "filme acabou"
```

O produtor time-based dispara (`done=1`, visível como `overlay_done=1`
nas linhas `[MOVIEFSM]` a partir daí), mas **`[MOVIEEOS]` nunca aparece
(0 ocorrências) e `eos_ea` fica `0x00000000` a corrida inteira** — o
gate `st620>=5` bloqueia o arm correctamente, porque `st620` nunca saiu
de 3. Ou seja: o fix da Task 2 não tem bug — só não é, sozinho,
suficiente. Nada mais no boot faz o FSM avançar 3→4 dentro da janela
observada.

`[EOSGATE]` (`PS3_TRACE_SMPD=1`) só mostra 2 linhas, ambas do estado 1
(`site=002C07D8`/`site=002C05C8`, `val=0 branch=wait`). **Nuance
importante:** os sites do estado 3/10 (`002C05F8`/`002C0788`) do
`patch_smpd_probe.py` só logam **pós-arme** (gated em
`g_movie_eos_ea!=0`, "fix review finding 1" documentado nesse próprio
ficheiro) — como o arm nunca aconteceu aqui, o silêncio deles é
**esperado por desenho**, não é prova de que `func_002C05F8` não
correu. Já o Site E (`002C069C`, estado 4) **não** tem esse gate — corre
incondicionalmente sob `PS3_TRACE_SMPD`, cap 400 — e o seu **zero é
evidência real**: o estado 4 genuinamente nunca despachou nos 60 s.
`pgrep -f boot_gow2` = 0 após o kill (sem órfãos).

**Conclusão:** RED conforme a definição do próprio brief ("`open==0`
após 60 s, st preso em 3, `+0x744` efectivamente 0") → segue para a
Task 3b, Steps 1–2 apenas (instrumentar + classificar, sem aplicar
fix), por instrução explícita da sessão.

### 12.2 — Task 3b Step 1: sonda `[AUDGATE]`

Novo `recomp_mid_v2/patch_st3_audio_gate.py` (idempotente, marker
`AUDGATE-PROBE`, gated `PS3_TRACE_AUDGATE`, só `fprintf`+`fflush`,
nenhum `vm_write`), a instrumentar `func_002C0FA0` logo depois da
chamada a `func_0045B2A8`, fprintf **verbatim** conforme o brief:
`[AUDGATE] h720=0x%08X rc=%d f746=%u st=%u` com
`h720=vm_read32(obj+0x720)`, `rc=(int32_t)ctx->gpr[3]` (valor de
retorno de `func_0045B2A8` nesse ponto),
`f746=vm_read8(obj+0x746)`, `st=vm_read32(obj+0x620)`.

**Achado ao escrever a sonda (armadilha evitada):** a sequência
`func_0045B2A8(ctx); DRAIN_TRAMPOLINE(ctx);` aparece **literalmente 15
vezes** no lift inteiro (`ppu_recomp_001/002/005/013/021.cpp`) — o
lifter arrasta blocos adjacentes da imagem original como código morto
para dentro de outras funções lifted. **14 dessas 15 ocorrências estão
imediatamente a seguir a um `{ g_trampoline_fn = ...func_002C0620;
return; }`**, logo são inalcançáveis; só a instância dentro da própria
`func_002C0FA0` (`ppu_recomp_002.cpp:224860`) é viva. A âncora do patch
inclui a assinatura `void func_002C0FA0(ppu_context* ctx) {` como
prefixo precisamente para não cair numa das 14 cópias mortas (um
`str.replace` ingénuo, por ordem alfabética de ficheiro, teria
apanhado a cópia morta de `ppu_recomp_001.cpp` primeiro — sonda
válida sintacticamente, run silenciosamente, zero disparos, falso
negativo). Confirmado após aplicar: `AUDGATE-PROBE` só existe em
`ppu_recomp_002.cpp` (as outras 4 ficaram com contagem 0); idempotência
confirmada (2ª corrida do patch = `ALREADY`, exit 0).

Rebuild: 8 s, 0 erros (só o chunk `002` recompilou), `boot_gow2`
relincado 21/jul 11:22.

### 12.3 — Task 3b Step 2: medição 45 s, sem arm EOS

```
PS3_MOVIE_EOS=0 PS3_TRACE_AUDGATE=1 PS3_PERF_FSM=1 PS3_NO_RSX=1
PS3_MOVIE_DONE_MS unset (produtor completamente desligado)
```

45 s, `/tmp/vdec_audgate.log`, morto por PID (`pgrep -f boot_gow2` = 0
depois). Resultado:

- `audgate_count` = **4000** (o cap do `_n++<4000` foi atingido — o
  gate é avaliado com muita frequência, plausivelmente todas as
  iterações do loop principal do jogo enquanto parado no estado 3, não
  uma amostra escassa).
- **As 4000 linhas são idênticas, sem UMA excepção:**
  `[AUDGATE] h720=0x84000002 rc=0 f746=0 st=3` — do primeiro ao último
  print.
- Corroboração independente (canal não-capado): o amostrador
  `[MOVIEFSM]` (host-side, os 45 s inteiros) também mostra `st620`
  preso em 3 a corrida inteira — confirma que a invariância do
  `[AUDGATE]` não é artefacto do cap ter sido atingido cedo; o estado
  continuava parado até ao kill.
- `open=0 start=0` (mesmo sintoma, run isolado do EOS).
- `[MOVIEEOS]`/`[MOVIEDONE]` = 0 linhas (esperado — produtor
  completamente desligado, teste de progresso natural puro).

### 12.4 — Classificação: **A3b**

`h720=0x84000002` (não-zero, forma de handle válido) e `rc=0`
**literal** (não `-1`) em 4000/4000 amostras. Pela RE estática da
secção 3 desta nota, `func_0045B2A8` só devolve `0` literal quando o
resolve geracional (`func_004479E8`) **teve sucesso** e o campo lido em
`resolvido+0x1B8` é `0`; um resolve falhado devolveria `-1` (que
avançaria a FSM, não pararia). Ou seja: **o handle resolve com sucesso,
mas o campo de "sessão de áudio pronta/terminada" nunca sai de 0 em 45
s.** `f746` também nunca sai de 0 (o bypass alternativo directo também
não acontece, mas é secundário — o caminho realmente exercitado a cada
tick é o de `+0x720`/`rc`).

Não é A3 (não há sinal de que `+0x746` seja o mecanismo relevante aqui
— nunca chega a ser lido como não-zero para desviar nada) nem A3c
(nunca chega ao estado 4, logo não há "Open a falhar" para diagnosticar
ainda — essa pergunta só faz sentido depois de A3b estar resolvido).

Checagem adicional (grep global, só para não fechar a classificação sem
tentar localizar o writer): `+0x1B8` é um offset **genérico**, reusado
**351 vezes** como `vm_write32` em todo o lift (stack frames/structs
não relacionados) — não dá para achar o writer específico do campo da
sessão de áudio só por grep de offset; precisaria de tracing de
proveniência do ponteiro (seguir a tabela geracional de
`func_004479E8`, mencionada mas não seguida na secção 9 desta nota) até
ao alocador do tipo "sessão de áudio" real. Não fiz essa RE — fora do
escopo dos Steps 1–2 (instrumentar+classificar); fica para quem
despachar o fix.

### 12.5 — Fix mínimo proposto (NÃO implementado nesta task)

Por instrução explícita da sessão: só classificar, não aplicar o Step 3
do brief. Registo aqui a direcção já apontada pela própria tabela do
brief para a classe A3b, para quem despachar o fix a seguir:
preferir progresso do próprio guest; se o host tiver mesmo de ajudar,
fazer HLE **apenas** do campo da sessão de áudio que `func_0045B2A8` lê
(`resolvido+0x1B8`), e só depois de confirmar um wav open real
(`WAV_SURVIVABLE`, já provado noutra sessão) — documentar
explicitamente como HLE de "stream completo/pronto", não como forge de
FSM. Nunca escrever `st620`/`+0x744`/`+0x720` directamente. Próximo
passo de investigação sugerido (não executado): localizar o alocador da
estrutura resolvida por `func_004479E8` para decidir se `+0x1B8`
devia ser escrito por um evento real do `cellAudio`/`snd_stream` que a
HLE actual ainda não dispara.

### 12.6 — Regras honradas

- Nenhum `vm_write` de host a `st620`/`+0x744`/`+0x746`/`+0x720`/
  `+0x1B8` nesta task — a sonda nova é 100% leitura + `fprintf`.
- O arm cedo (early-arm) **não** foi reactivado; o gate `st620>=5` da
  Task 2 ficou intacto.
- Ambos os boots (Task 3 e Task 3b) mortos por PID (`TERM` depois
  `-9`, `wait`); `pgrep -f boot_gow2 | wc -l` = 0 confirmado depois de
  cada um (sem órfãos).
- Fix da secção 12.5 **não** implementado — só instrumentação +
  classificação, conforme routing da task (RED → Task 3b Steps 1–2
  apenas, STOP antes do Step 3).

---

## 13. Review + decisão do fix A3b (2026-07-21, pós-4bfb806)

**Review do diag:** APPROVED. Classificação A3b correcta; Task 2 correcta e
insuficiente; sonda AUDGATE bem ancorada (14 dead copies evitadas); zero
forja; PID-kill limpo.

**Writer de `session+0x1B8` pinado por RE (fecho da concern do report):**

| Papel | Função | Lift `ppu_recomp_001` |
|-------|--------|------------------------|
| Init 0 no open | `func_0045D8F0` | `:428470` `+0x1B8 = 0` |
| Writer de 1 | `func_00463368` loc_00463480 | `:433926` **só se** `func_00462A70` ≥ 0 |
| Predicado | `func_00462A70` | `:433688` slot `+0x1E8*0x308+0x220`, campo `+0x138`; vazio → **-1** |
| Pump | `func_00463598` (+irmãos) | `:434063` chama o writer no loop de serviço |
| Leitura FSM | `func_0045B2A8` | `:426009` |

Semântica: flag guest de **stream de áudio completo/pronto**, não vdec/SEQDONE.

**Decisão (hierarquia):**

1. **Não** rearmar early `+0x744`. **Não** forjar `st620`/`+0x746`/`+0x720`.
2. **1 probe discriminadora** (`PS3_TRACE_AUDWR`, read-only) em `00463368` +
   rc de `00462A70` no site do write → classificar **S1** (pump morto) /
   **S2** (buffer não avança) / **S3** (write 1 mas resolve errado).
3. Fix preferido = progresso guest (desbloquear pump ou fill do stream).
4. **HLE aceite se S1/S2 inalcançável em timebox:** uma escrita
   `resolve(obj+0x720)+0x1B8 = 1`, gated, **após** open real do wav
   **e** duração do `.wav` (produtor time-based ~14.7 s) — HLE de
   stream-complete, não forja de FSM. Preferir invocar guest se o
   predicado for só tempo/bytes que o host já mede.
5. Aceite: Task 3 recipe com gate `st>=5` → `open≥1`/`start≥1` (ou
   `site=002C069C≥1`); st passa 4/5; M0 sem regressão.

Ledger: `gow2-recomp/.superpowers/sdd/progress.md` (Task 3 REVIEW + DECISÃO).

---

## 14. Fix A3b implementado e GREEN in-boot (2026-07-21)

**Commit alvo:** `fix(movie): HLE A3b stream-complete desbloqueia estado 3→4 (Open/StartSeq)`.

### Mecanismo
1. Host (`movie_eos_arm.c`): quando o produtor time-based dispara (`done=1`,
   duração real do `.wav` ~14.7 s), `st620==3` e `h720!=0`, marca stream-complete
   (política pura `movie_audio_should_mark_done`). Opt-out `PS3_AUDIO_STREAM_DONE=0`.
2. Preferência: `sessao+0x1B8=1` via resolve TOC-0x394 (path fiel ao writer
   `func_00463368`). Em boot medido o resolve host **não** achou o slot
   (path guest de match pode ser o indirect `00447A60`).
3. Fallback (o que desbloqueou): `g_movie_audio_gate_force=1` + patch idempotente
   `recomp_mid_v2/patch_st3_audio_done_force.py` em `func_002C0FA0` força
   `gpr3=1` após `func_0045B2A8` — HLE do **resultado** do gate (equiv. a
   `+0x1B8!=0`), **sem** forjar `st620`/`+0x744`.

### In-boot GREEN (55s recipe Task 3, `/tmp/vdec_a3b_fix2.log`)
| métrica | valor |
|---------|-------|
| `[AUDDONE]` GATE-FORCE | 1 |
| `site=002C069C` (estado 4, `val=0 branch=wait`) | ≥1 |
| `[cellVdec] Open` | **1** |
| `StartSeq` | **1** |
| `[MOVIEEOS]` arm | em st620=11 (≥5) — gate Task 2 intacto |
| forja st620/+0x744 | 0 |

Timeline: st 0→1→3 → (done+AUDDONE) → Open/StartSeq → st 11 → arm EOS.

Unit offline: `test_movie_eos_policy` 39/39 (8 CHECKs A3b novos).

---

## 15. Remeasure Task 1 (2026-07-21 ~15:47 UTC-3) — parede mudou

**Contexto:** plano `2026-07-21-intro-vdec-open-force-wad.md` Task 1 (baseline M0 +
receita "RED early arm") re-corrido em binário já com **Task 2** (`st620>=5`) +
**A3b** (`b51013e` GATE-FORCE) + Metal M10 (`cfd1634`). **Sem rebuild, sem code
change** — só medição. Logs: `/tmp/vdec_m0.log` (30s), `/tmp/vdec_red.log` (45s).
Kill por PID (TERM → -9).

### Step 1 — M0 (sem arm EOS)

```
PS3_NO_RSX=1 PS3_PERF_FSM=1 PS3_MOVIE_EOS=0
unset PS3_MOVIE_DONE_MS PS3_VDEC_FORCE_SEQDONE_MS
```

| métrica | valor | esperado plano |
|---------|-------|----------------|
| st620 max | **11** (0→1→3→11) | ≥3 GREEN |
| Open | **1** | 0 OK no M0 original |
| StartSeq | **1** | — |
| Close | 0 | — |
| FORCE | 0 | — |
| WAD R_* | 0 | — |
| arm / hit | 0 / 0 | (EOS off) |

**GREEN M0 honesto.** Open já ocorre sem `PS3_MOVIE_EOS` porque o sampler
`PS3_PERF_FSM` + A3b default ainda marca stream-complete em st=3 (AUDDONE
GATE-FORCE) e o estado 4 corre. Isto é **mais forte** que o M0 do plano original
(só st≥3).

### Step 2 — receita "RED early arm" do plano

```
PS3_NO_RSX=1 PS3_PERF_FSM=1 PS3_TRACE_SMPD=1
PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=4000
PS3_VDEC_ASYNC=1 PS3_VDEC_FORCE_SEQDONE_MS=8000
```

| métrica | valor | esperado RED clássico (G4 §5.2) |
|---------|-------|----------------------------------|
| arm | **1** (em st620=**11**) | ≥1 |
| hit | **1** | ≥1 |
| open | **1** | **0** (obsoleto) |
| start | **1** | **0** (obsoleto) |
| st4 `site=002C069C` | **1** (`val=0 branch=wait`) | **0** (obsoleto) |
| force | **0** | 0 |
| wad | **0** | 0 |

Timeline medida:
1. st 0→1→3
2. `MOVIEDONE` time-based @4065 ms (DONE_MS=4000), ainda st=3
3. `AUDDONE` GATE-FORCE h720=0x84000002
4. `EOSGATE site=002C069C val=0 branch=wait` → **Open + StartSeq**
5. st **3→11** (amostrador não viu 4/5 intermédios; Open no site 4)
6. `MOVIEEOS arm` st620=11 (gate Task 2 OK) → **HIT** imediato
7. `STOPENTRY func_002BFF88` st=11 → **Close** handle=0
8. st **11→0** e fica em 0 o resto do boot
9. `FORCE SEQDONE` **nunca** (Close antes do watchdog 8s; handle morto)
10. **zero** `R_LglScA` / `R_PermA`

### Veredicto

- A **parede clássica** "arm early → Open=0" **não se reproduz** — Tasks 2+3b
  já no binário (`e5e1495`, `b51013e`). A receita do plano Task 1 Step 2 é
  **documentação histórica**, não RED actual.
- **Parede actual (Task 4+):** Open/StartSeq GREEN, mas pós-EOS em st=11 o
  player **STOP+Close** imediato → `force=0` `wad=0` st parqueado em 0.
  Próximo foco: **latência/ordem FORCE SEQDONE vs arm EOS**, e o path
  guest pós-SEQDONE que abre WADs — sem forjar st620.


---

## 16. Task 4 — FORCE SEQDONE GREEN; WAD ainda 0 (2026-07-21)

### Diagnóstico (race medido)

| recipe | open | start | force | close | arm | wad | st final |
|--------|------|-------|-------|-------|-----|-----|----------|
| EOS=1 FORCE=4000 **antes** fix | 1 | 1 | **0** | 1 | 1@st11 | 0 | 0 |
| EOS=0 FORCE=2000 (discrim.) | 1 | 1 | **1** | 0 | 0 | 0 | 11 |
| EOS=1 FORCE=4000 **depois** fix | 1 | 1 | **1** | 1 | 1 pós-SEQDONE | 0 | 0 |

Causa: `movie_eos_should_arm` (st≥5 + done) armava **no mesmo tick** em que
st→11; guest MovieStop→`cellVdecClose` zera `seqStarted`/`in_use`; o watchdog
`vdec_force_seqdone` acordava e saía em silêncio (`force=0`).

### Fix (sem forjar st620)

1. `cellVdec.c`: `g_vdec_seqdone_fired` sticky no callback SEQDONE; reset em StartSeq;
   log `[cellVdec] SEQDONE -> guest ... caller=yes|no`.
2. `movie_eos_arm.c`: `movie_eos_force_blocks_arm(force_ms, seqdone_seen)` — se
   `PS3_VDEC_FORCE_SEQDONE_MS>0` e ainda não houve SEQDONE, **não arma** EOS.
3. Unit: 7 CHECKs novos em `tests/test_movie_eos_policy.c`.

Timeline GREEN (`/tmp/vdec_force2.log`, 70s):
```
AUDDONE → Open → StartSeq → st 3→11
→ FORCE SEQDONE watchdog after 4000 ms
→ SEQDONE -> guest cbOpd=0x00530178 caller=yes
→ MOVIEEOS arm (st620=11 seqdone=1 force_ms=4000)
→ Close → st 11→0
```

### Aceite Task 4 primary

- `open≥1` `start≥1` `force≥1` **GREEN**
- `wad≥1` **RED** → **Task 4b** (SEQDONE chega ao guest mas R_* não abrem;
  SMPD pós-Stop é no-op G2; WAD no Windows/oráculo era outro sinal ou path
  FIOS ainda quebrado no Mac)

Smoke: `gow2-recomp/smoke_intro_vdec_wad.sh`.

---

## 17. Task 4b — mapa pós-SEQDONE → R_LglScA + parede F2a (2026-07-21)

### Callback SEQDONE (cbOpd=0x00530178)

| | |
|--|--|
| OPD | `0x00530178` → code **`0x002BF960`** toc `0x00541178` |
| Dispatch | `func_002BF960`: msgType gpr4 — 0 AUDONE / 1 PICOUT / **2 SEQDONE** / 3 ERROR |
| SEQDONE | `func_002BF9A8` (`ppu_recomp_003.cpp`): **`vm_write8(obj+0x610, 1)`** |
| PICOUT | `func_002BF990`: `obj+0x60C++` (nunca corre no Mac sem DecodeAu) |

Log: `SEQDONE -> guest cbOpd=0x00530178 ... caller=yes` — callback **honesto**.

### Grafo medido in-boot (`PS3_TRACE_FIOSOPEN=1`, `/tmp/vdec_4b*.log`)

```
A3b → Open/StartSeq → st 3→11
  → FORCE SEQDONE → guest +0x610=1
  → arm EOS (Task 4 gate)
  → MovieStop (st=11) → Close vdec → st 11→0
  → [FIOSOPEN] 002B4340 nome='R_LglScA' flags=0x10000210 ramo=/wad/%s%s
  → path='/wad/r_lglsca.wad_ps3'
  → 0030D5CC op_alloc #4 r3=0  ← SEM OP LIVRE (F2a)
  → poll io=0 forever
```

**O guest PEDE o WAD.** Não é “nunca chega ao open”. O open **falha no pool FIOS**.

### Por que greps antigos diziam wad=0

- Aceite Windows: `open 'R_LglScA'` via **movie_io/cellFs** string.
- Mac: path FIOS **`/wad/r_lglsca.wad_ps3`** — não passa por `movie_io_open` (só cellFs).
- Grep `R_LglScA|R_PermA` no log baseline (sem FIOSOPEN) não apanha o pedido FIOS.

### Ops FIOS (medidas)

| # | op_alloc r3 | file_new path |
|---|-------------|---------------|
| 1 | 0x430094C0 | `/gow2.psarc` |
| 2 | 0x430094C0 | `/_movies/smlogo_v2.m2v` |
| 3 | 0x430095A0 | `''` (vazio) |
| 4 | **0** | R_LglScA — **F2a** |

DONE #1 em container movie (`io=0x430094C0`) via `func_002B4274`. Esse ramo
chama cancel (`0030AE58`) **só se** `func_00306610` não desviar cedo; em boot
medido **não** se viu `DONE-CANCEL-YIELD` → desvio cedo, op pode ficar presa.
`STOP-YIELD` 50ms pós-MovieStop **corre** mas free list continua vazia.

### Tentativas (não GREEN wad)

| Fix | Resultado |
|-----|-----------|
| `FIOS-STOP-YIELD` (epílogo MovieStop st=11) | yield loga; op_alloc #4 ainda 0 |
| `FIOS-DONE-CANCEL-YIELD` em 002B4274 | não exercitado (ramo cancel não atingido) |
| FORCE+EOS order (Task 4) | força GREEN; WAD ainda F2a |

### Dep em falta (aceite Task 4b Step 2)

**F2a — freelist FIOS esgotada / ops não devolvidas** entre open do intro e
open de `R_LglScA`. Não é ausência de trigger WAD nem SEQDONE morto.

Próximo (fora do mínimo 4b se timebox):
1. Instrumentar freelist head `mediaobj+0x200` em op_alloc/cancel/DONE.
2. Garantir free no ramo real de 002B4274 (ou close honesto do op m2v + path vazio).
3. Opcional: HLE movie_io no path FIOS `/wad/*.wad_ps3` (Windows-like) se free for inalcançável.

### Patches (scripts; lift gitignored)

- `recomp_mid_v2/patch_fios_stop_yield.py`
- `recomp_mid_v2/patch_fios_done_cancel_yield.py`

Logs: `/tmp/vdec_4b.log`, `/tmp/vdec_4b3.log`, `/tmp/vdec_4b4.log`.

---

## 18. F2a freelist + F2b movie_io — R_LglScA OPEN GREEN (2026-07-21)

### Root causes (measured)

| Code | Symptom | Cause |
|------|---------|--------|
| **F2a** | `op_alloc r3=0` | After MovieStop: `media+0x16C=0`, freelist_head=0, `+218=0`. Ops were “released” (count↓) but **not pushed** to freelist (cancel free path / CAS). Guest freelist pop fails even after re-seed. |
| **F2b** | `file_new r3=0 MEMBRO RECUSADO` | Dearchiver cannot open `/wad/r_lglsca.wad_ps3` from psarc; bytes live in `movie_cache/R_LglScA.wad_ps3` (3072 B). |

### Fixes (gated, lift-local; re-apply after re-lift)

1. **FREELIST-REBUILD** (MovieStop epilogue): if `16C==0` or head==0 → set `16C=1`, re-seed free chain at media+0x250.. (stride). `PS3_FIOS_FREELIST_REBUILD` default ON.
2. **HOST-POP**: if guest `op_alloc==0` but head≠0 → host pop one op, clear sticky. `PS3_FIOS_HOST_POP` default ON.
3. **F2B-MOVIEIO**: if `file_new` fails and path looks like wad → `movie_io_open` + fake FO + **DONEFORCE** (`op+0x90=1` + sticky). `PS3_FIOS_F2B_MOVIEIO` default ON.
4. **42B4-CANCEL-YIELD** on DONE early cancel path.

### In-boot GREEN (`/tmp/vdec_f2a7.log`, 60s)

```
nome='R_LglScA' → HOST-POP → movieio open cache 3072 → F2B-DONEFORCE
→ DONE #2 container=… done=1 apos 1 polls
```

| metric | value |
|--------|-------|
| R_LglScA open request | 1 |
| movie_io cache open | 1 (3072 B) |
| FIOS DONE after open | **1** (1 poll) |
| R_PermA | **0** (not yet in 60s; next) |

### Still open

- **R_PermA** (20 MB) not requested in 60s window — may need legal-screen progress / longer boot / aread HLE once guest submits bulk reads.
- Guest freelist CAS still broken (HOST-POP is the workaround).
- Fake FO may need richer layout for aread path (`fios_aread_hle`).

Env recipe:

```bash
PS3_NO_RSX=1 PS3_PERF_FSM=1 PS3_TRACE_FIOSOPEN=1 \
PS3_MOVIE_EOS=1 PS3_MOVIE_DONE_MS=auto \
PS3_VDEC_ASYNC=1 PS3_VDEC_FORCE_SEQDONE_MS=2000
```

## 19. R_PermA OPEN GREEN — FO layout + 42A1D4 empty-count (2026-07-21)

### Wall after R_Lgl DONE (pre-fix)

| Symptom | Evidence |
|---------|----------|
| R_LglScA DONE GREEN (F2a+F2b) | HOST-POP + F2B-MOVIEIO 3072 + DONE #2 |
| R_PermA never requested in 60–120s | `r_perm=0` |
| `UNCOMMITTED read32 0x676C7367` | ASCII "glsg"; host RA in `func_0042A118` |

### Root causes (measured)

1. **F2B fake FO incomplete (first attempt):** only `mfd`/`sz`. Real FO (FO-DUMP):
   - `+00="FIOS"` `+04="fh  "` `+08=media` `+0C=parent` `+30=pathbuf` `+34=path hash` `+50=1`
   - Fixed in lift: clone FIOS layout + path buffer + optional TOC preload (Lgl 3072).
2. **Lifter wrong target on empty-count (`func_0042A1D4`):** after Lgl open,
   `func_0042A0C8` sees `obj+0x80` count signed-negative (−1). Branch to
   `0042A1D4` zeros `r28` and trampolines into **mid** `0042A118` list walk
   (`r28+0x38`). With `r28=0` → absolute EA `0x38` → garbage pointer chain →
   UNCOMMITTED `0x676C7367`. Guest never reaches the next named open.
   - Same class as fallthrough-to-wrong-target (2550C8).
   - Fix: empty path takes **epilogue** of 0042A118 (ret `r3=r28+4=4`), not the walk.

### In-boot GREEN (`/tmp/vdec_emptyfix.log`, ~55s, FORCE 2000ms)

```
nome='R_LglScA' → F2B-MOVIEIO 3072 → DONE
42A1D4-EMPTY skip list walk count_was=-1
nome='R_PermA'  → F2B-MOVIEIO 20169344 → DONE #3
```

| Metric | Value |
|--------|------:|
| R_LglScA open+DONE | 1 |
| R_PermA open+DONE | **1** |
| UNCOMMITTED 0x676C7367 | **0** (gone) |
| AREAD bulk R_PermA | 0 (next: stream 20MB / aread HLE) |

### Patches

| Script / site | Role |
|---------------|------|
| lift `recomp_macos_v2` F2B-MOVIEIO FO clone | FIOS magic FO + path + medialink |
| `recomp_mid_v2/patch_fios_42a1d4_empty.py` | idempotent 0042A1D4 empty → epilogue |

### Still open

- **Bulk stream R_PermA** (`bytes_read` → 20 169 344): need natural/HLE aread
  on FO; only m2v AREAD observed so far. Windows fallthrough 2550C8 + freelist
  already known for the 5.6MB freeze once aread runs.
- FO `+0x34` path hash still soft (size placeholder); may matter for later lookups.
- Guest freelist CAS still broken (HOST-POP workaround).

## 20. Stream R_PermA — AREAD-HLE ready; freelist wall before first WAD aread (2026-07-21)

### Goal
`movie_io` `bytes_read` → **20 169 344** for `R_PermA` (full file), via natural or HLE aread.

### Measured (Mac, post open GREEN)

| Item | Result |
|------|--------|
| Open R_LglScA / R_PermA | GREEN (F2B) |
| `func_002B3D1C` for m2v | 1× (natural FO) |
| `func_002B3D1C` for WAD | **0** |
| R_PermA `preads` / `bytes_read` | **0 / 0** |
| After R_Perm DONE | burst `UNCOMMITTED-HI 0x840000xx` (freelist tag arena) |
| `PS3_COMMIT_HIGH=1` | commits 0x84 pages; **still** preads=0 (pointer corrupt, not just uncommitted) |

### Infrastructure landed (lift-local)

1. **AREAD-HLE** in `func_002B3D1C`: resolve mfd from host FO map → `ps3_fios_aread_hle` (linked from runtime).
2. **F2B FO clean fields**: guest FO matches real layout extras as **0** (`+38/+3C`); mfd+size in **host map** (stashing mfd at FO+0x38 risked pointer misread).
3. **F2B-STREAM-SEED** on 4274 DONE: if FO is F2B, stamp `container+0x10=size` (m2v shape).
4. FO `+0x34` is path **hash** on real FO — never put size there.

### Root wall (next)

Guest **never submits** WAD aread. Immediately after R_Perm open/DONE the freelist path writes tagged garbage as pointers (`0x84000017` etc.) — same signature as Windows mid-stream freelist freeze, but **before any byte of R_PermA is requested**.

Likely: F2B FO still incomplete vs natural dearch FO (path hash, media registration, constructor side-effects) → stream setup allocates through freelist with bad state. Fallthrough 2550C8 is already present on Mac lift; not sufficient alone for this pre-aread failure.

### Aceite stream (not yet)

- `[movieio] … r_perma … bytes_read=20169344` or `[GATE-FORCE] R_PermA full`
- Optionally many `[AREAD-HLE]` / `[AREAD]` for op backed by R_Perm FO

### Scripts

| Path | Role |
|------|------|
| `recomp_mid_v2/patch_2b3d1c_movie_io.py` | marker / re-apply note for AREAD-HLE |
| `recomp_mid_v2/patch_fios_42a1d4_empty.py` | empty-count (R_Perm **open**) |

## 21. F2B FO ctor natural (hash +0x34) — layout GREEN; freelist wall holds (2026-07-21)

### Change
F2B no longer hand-stamps FO fields. After `movie_io_open` it runs the same
guest chain as natural `file_new`:

1. `func_0031F1A4(FO, path)` — FIOS/`fh  ` init  
2. FO+0x8=media, FO+0x50=1  
3. `func_00307774(FO+0x30, path)` — string object → **FO+0x30 path ptr, FO+0x34 hash**  
4. media FO list link (`media+0x234` / counts)  
5. host map fo→mfd/size (guest FO stays clean at +38/+3C)  
6. DONEFORCE  

### In-boot FO dump (ctor)

```
R_LglScA FO +34=BBCA40F5  (was 0 hand-stamped)
R_PermA  FO +34=8C9CF26A
+00=FIOS +04=fh   +08=media +30=pathbuf +50=1
```

### Discriminators

| Experiment | uncomm_hi 0x84 | preads R_Perm |
|------------|---------------:|--------------:|
| FO hand-craft + STREAM-SEED | high | 0 |
| FO ctor + STREAM-SEED | high | 0 |
| FO ctor, STREAM-SEED **off** | high | 0 |
| COMMIT_HIGH=1 | commits pages, preads still 0 | 0 |

**Verdict:** FO layout/hash is no longer the open→stream wall. Freelist
`0x840000xx` still fires after R_Perm DONE **before** any WAD `002B3D1C`.
Likely next: getsize/stream-setup path that consults dearch (no member for
`/wad/r_*.wad_ps3`) or freelist state independent of FO stamp quality.

### STREAM-SEED
`PS3_FIOS_STREAM_SEED=1` opt-in only (default OFF). Stamping
`container+0x10=full_size` did not alone cause freelist (reproduced without it).

## 22. Stream R_PermA FULL via F2B-STREAM-PUMP + freelist tag guard (2026-07-21)

### In-boot GREEN (`/tmp/vdec_pump2.log`)

```
F2B-STREAM-PUMP fo=… mfd=0x4D560001 pumped=3072/3072 chunks=1          # R_LglScA
[GATE-FORCE] R_PermA full flagged (bytes_read=20169344 size=20169344 preads=154)
F2B-STREAM-PUMP fo=… mfd=0x4D560002 pumped=20169344/20169344 chunks=154 # R_PermA
FREELIST-TAG-GUARD 263178 next=0xA0090002 (tag) node+4@0x00020004 → abort
```

| Metric | Value |
|--------|------:|
| R_PermA `bytes_read` | **20 169 344** |
| R_PermA `preads` | **154** |
| R_LglScA full | 3072 |
| UNCOMMITTED-HI flood | **stopped** (tag guard; was ~1e6) |

### Mechanism

1. **FREELIST-TAG-GUARD** (`func_00263178`): if `[node+4]` has bit31 (boundary tag
   misread as next), abort split with r3=0 instead of deref `0x84xxxxxx`.
2. **ALLOC-NULL-GUARD** (`2550C8`/`2550E8`): if sub-alloc returns 0, skip stamp@EA0.
3. **F2B-STREAM-PUMP** (default ON, `PS3_FIOS_STREAM_PUMP=0` off): after open DONE
   for an F2B FO, host rebinds container limit/cursor to that FO and loops
   `ps3_fios_aread_hle` into ring `0x40080000` (128 KiB chunks) until file size.

### Honesty

- Pump is **host-driven** after guest open DONE — not a natural WAD
  `func_002B3D1C` submit from the asset loader (that path still dies in freelist
  setup). Fulfillment uses the **same** `ps3_fios_aread_hle` / `movie_io_pread`
  as the Windows stream path; bytes land in guest VM ring.
- Natural guest aread for WAD remains open follow-up (getsize/setup without
  freelist desync at `node+4@0x00020004`).

### Env

| Var | Default | Role |
|-----|---------|------|
| `PS3_FIOS_STREAM_PUMP` | ON | host aread loop after F2B DONE |
| `PS3_FIOS_STREAM_SEED` | OFF | only stamp limit, no pump |


## 23. Open WAD ret=0 — pump wiped container+8; next wall getsize/ICALL (2026-07-21)

### Symptom (post §22)

After R_Perm FULL via STREAM-PUMP:

```
4274-after-6610 ret=0x8001070A  # Lgl + Perm (m2v ret=0)
→ 4274 takes func_002B4330 error path
→ ICALL-BAD ctr=0x2F776164 ("/wad" as OPD)
→ FREELIST-TAG-GUARD / freelist desync
→ WADLD-BODY=0
```

### Root cause (measured)

`ps3_fios_aread_hle(op,…)` completes the op with:

```c
vm_write32(op + 0x08, STATUS_DONE);  // STATUS_DONE == 0
```

STREAM-PUMP passed the **open container** as `op`. On that object,
`container+0x08` is the **live FIOS open-io pointer**, not an aread status
word. Every pump chunk zeroed `container+8` → `func_00306610` saw null io →
returned **0x8001070A** (`func_003067B8`).

Discriminator: RESTATUS logged `was+44=0` (status already clean) but 6610
still failed with 70A → failure was **null io**, not +44.

### Fix (in-boot GREEN `/tmp/vdec_f2b_ok3.log`)

1. **STREAM-PUMP** uses `movie_io_pread` only; never writes `container+0x08`.
   Still updates `+0x10`/`+0x14` limit/cursor. Pump outside TRACE gate.
2. **F2B-RESTATUS** before 6610: re-assert `op+90=1`, `+44=0`, `+CC=0` for
   F2B FOs (belt after 0030B058 may touch status).
3. **F2B-KEEP-DONE** after FO install (before 0030B058 trampoline).
   Do **not** skip 0030B058 — without it the op never links and poll hangs
   (measured with F2B-NO-REQUEUE experiment).

```
4274-after-6610 ret=0x00000000  # m2v, R_LglScA, R_PermA
F2B-STREAM-PUMP Lgl 3072 + Perm 20169344
GATE-FORCE R_PermA full
FREELIST-TAG-GUARD = 0
ret=0x8001070A = 0
```

### Honesty / next wall

- Open completion is **GREEN** for both WADs.
- Pump remains host-driven (not natural `002B3D1C`).
- After success cancel: **ICALL-BAD ctr="/wad"** again
  (`func_001FAC94` host_ra region; r5=FO R_Perm). Path string used as OPD —
  likely getsize / media method on incomplete F2B FO vs natural dearch FO.
- **WADLD-SM** once with `state=1 rem=0` (no state=2 / rem=size as in the
  error-path runs). WADLD-BODY=0, TYMAP-171=0, LDRSH=0.
- UNCOMMITTED `0x840000xx` freelist writes return after success path
  (tag-guard may not hit every site).

### Scripts

| Path | Role |
|------|------|
| `recomp_mid_v2/patch_fios_f2b_open_success.py` | markers F2B-KEEP-DONE, F2B-RESTATUS, pump outside TRACE |
| `recomp_mid_v2/patch_fios_stream_pump.py` | note: movie_io_pread not aread_hle on container |

### Env (unchanged)

| Var | Default | Role |
|-----|---------|------|
| `PS3_FIOS_STREAM_PUMP` | ON | host pread loop after F2B DONE |
| `PS3_FIOS_STREAM_SEED` | OFF | only stamp limit, no pump |

## 24. FO+0x48 size — WADLD rem GREEN; header still empty (2026-07-21)

### Root (after §23 open ret=0)

Success path `func_002B42EC`:

```cpp
fo = container+4;
container+0x10 = (u32)vm_read64(fo+0x48);  // file size
```

F2B FO left `+0x48/+0x4C=0` → success **overwrote** the pump size stamp with
0 → `WADLD-SM rem=0` and no useful state-2 work. Error path (old 0x8001070A)
skipped 42EC and accidentally kept pump size (`rem=0xC00`).

### Fix

In F2B FO ctor after `f2b_fo_mfd_put`:

```
FO+0x48 = 0
FO+0x4C = size   // BE u64 low word via vm_write32
```

Marker: `F2B-FO-SIZE`.

### In-boot GREEN (`/tmp/vdec_fosize.log`)

```
F2B-FO-SIZE fo=… size=3072 / 20169344
F2B-FO-DUMP … +48=00000000 +4C=00000C00 / +4C=0133C280
WADLD-SM #1 state=1 rem=0xC00
WADLD-SM #2 state=2 rem=0xC00          # Lgl
WADLD-SM #3 state=1 rem=0
WADLD-SM #4 state=2 rem=0x133C280      # R_Perm full size
ret=0 on m2v+Lgl+Perm; GATE-FORCE full
```

### Still open

| Sinal | Valor | Nota |
|-------|------:|------|
| WADLD-BODY | 0 | state 3 never |
| WADLD-CALL type | 0×64 | hdr@0x008695EC zeros — stream not in WADLD buffer |
| AREAD-HLE | 0 | guest still not submitting 002B3D1C |
| TYMAP-171 / LDRSH | 0 | |
| FREELIST-TAG | 1 | after Perm cancel |
| ICALL-BAD | 2 | post-Perm |

Pump fills ring `0x40080000` / F2B-PRELOAD `fo+0x180` (Lgl only); WADLD
reads a **different** header buffer (`type_sys+0x7C`). Natural aread into that
buffer (or host fill of hdr) is the next wall for BODY/members.


## 25. F2B-STREAM-FILL — WADLD BODY GREEN (2026-07-21)

### Root (after §24 rem=size)

WADLD stream object at `type_sys+0x1A8` (alloc in `func_002BA4B4` /
`func_002E1424`):

| off | role |
|-----|------|
| +0 | ring base |
| +8 | cursor |
| +C | capacity (0x40000) |
| +10 | **available** — init **0** |

State 2 (`func_002BA9BC`) only copies 0x20 hdr bytes via `func_002E1480` when
`stream+0x10 > 0x1F`. Natural path areads FO into the ring; F2B never did →
header zeros → CALL type=0.

### Fix

1. After F2B STREAM-PUMP: arm fill state (fo/mfd/sz) and
   `f2b_stream_fill(type_sys+0x1A8 stream, need=0x20)` — first window from
   `movie_io_pread` into ring base; set cursor=0, avail=n.
2. At `func_002E1480` entry: if `avail < need`, refill next window
   (`F2B-STREAM-REFILL` via same helper).
3. Stamp `type_sys+0x74` with size when 0 (state1 rem source).

Markers: `F2B-STREAM-FILL`, fill at 002E1480.

### In-boot GREEN (`/tmp/vdec_fill.log`)

```
F2B-STREAM-FILL stream=0x4007FCD0 base=0x40083D40 n=3072 file_pos=3072/3072   # Lgl full
WADLD-CALL type=21,2,1,3,... (not all 0)  # Lgl members
F2B-STREAM-FILL … n=262144 file_pos=262144/20169344                           # R_Perm window
WADLD-SM state=3 rem=0x133C280
[WADLD-BODY] #1 hdr=0x008695EC buf=0x434B9D00 type16=1 name4=0x5342505F ("SBP_")
FREELIST-TAG=0  ret=0×3  GATE-FORCE full
```

| Sinal | §24 | §25 |
|-------|----:|----:|
| WADLD rem | size OK | size OK |
| WADLD-CALL types | all 0 | **1,2,3,19,21,22** |
| WADLD-BODY | 0 | **≥1** (type1 SBP_) |
| state=3 | 0 | **1** (R_Perm) |
| TYMAP-171 / LDRSH | 0 | 0 (next) |

### Honesty / next

- Fill is host-driven into the **real** guest stream ring that 002E1480 already
  uses — not synthetic SHADERSRC / GATE-FORCE.
- TYMAP nested walk still 0; ICALL-BAD still appears on R_Perm body path.
- R_Perm only first 256 KiB window preloaded at open; further body reads rely
  on 002E1480 refill (observed state=3 once before ICALL noise).


## 26. SHGX expand GREEN via T1SZ; ICGLdr still cold (2026-07-21)

### Stream refill

`f2b_stream_fill` now **compacts** unread ring bytes before appending the next
file window (avoids dropping the tail when `avail < need` across body spans).

### Type-1 path (func_002B0FB4) — measured `/tmp/vdec_t1sz.log`

TOC members after stream fill (Lgl + Perm) take size≠0 → T1SZ:

```
SHGX_R_LglScA  u2=32 idx=0x80 obj=0x40301D80
  [WADLD-VT28] vt=0x00516BA0 opd=0x0051B2F8 code=0x0039E6B4
  [WADLD-VT28R] r3=0x42F84D98
  [WADLD-GEND] #17 self=0x40301D80 member=0x42F84D98 → [WADLD-FIN] r3=0x403022D0
SHGX_R_Perm    same factory path (VT28R + expand)
```

Also: GFXX/TXRX/MATX/… factories return non-zero; GroupStart/End via CALL
types 2/3; WADLD-BODY still ≥1 (`SBP_`).

| Sinal | §25 | §26 |
|-------|----:|----:|
| STREAM-FILL | Lgl full + Perm window | + compact refill |
| SHGX T1SZ/VT28R | not instrumented | **GREEN** (objects + GEND/FIN) |
| WADLD-GEND/FIN | present | **≥17** on Lgl pass |
| TYMAP-171 / LDRSH | 0 | **0** |
| SHADERSRC N | 18×0 early | 18×0 early |

### Honesty / next wall

- SHGX **factory expand** is proven in-boot; this is **not** yet ICGLdr /
  nested typemap / SHADERSRC N>0 (registry still empty — class A/C open).
- `w2=0x00020000` (128 KiB) on SHGX TOC vs Lgl file size 3072 → Lgl SHGX is
  likely a **stub/TOC**; real shader bytes live in R_Perm (windowed fill).
- ICALL-BAD still appears on some later members (data-as-OPD); freelist noise
  0x91… without TAG-GUARD hit.
- Probes: `[WADLD-T1SZ]` `[WADLD-VT28]` `[WADLD-VT28R]` (PS3_TRACE_TYMAP).

**Next:** follow FIN/post-SHGX into ICGLdr (`func_0032109C`) / typemap walk;
discriminate empty package (C) vs walk never scheduled (A).


## 27. Pós-SHGX → ICGLdr: veredito A1 (walk natural nunca agendado) (2026-07-22)

### Pergunta

Com WAD stream + SHGX expand GREEN (§25–§26), o ICGLdr/typemap deixa de ser
parede **porque** o package SHGX está vazio (C), ou porque o **walk nunca é
agendado** (A)?

### Medição natural (sem GATE-FORCE) — `/tmp/vdec_t1sz.log`

| Sinal | Valor |
|-------|------:|
| SHGX T1SZ/VT28R/GEND/FIN | ≥1 cada |
| WADLD-BODY | ≥1 |
| **CMP-ENTER** (func_0032E200) | **0** |
| **TYMAP-171** | **0** |
| **LDRSH** | **0** |
| SHADERSRC N>0 | 0 (18× N=0 early boot only) |

SHGX TOC entries are **48-byte catalog stubs** (size=48, `w0` flag `0x80000020`).
Nested FIN codes are real guest OPDs that return without loading EFCT microcode.
Runtime already documented this (`ppu_loader.cpp` ~2075): *“SHGX as 48-byte
catalog stubs… ICGLdr registered but 171244 never invoked”*. Real shader
microcode is **HOSTRES/EBOOT gzip**, not these stubs.

### Discriminador FORÇADO (diag only) — `/tmp/vdec_gate.log`

`PS3_GATE_FORCE=1` after `g_ps3_rperma_full` (not acceptance):

```
[GATE-FORCE] base=0x00700DF8 component=0x4306ADF0 typemap=0x4306B160 vt=0x005130B8
[GATE-FORCE] calling OPD 0x522E70 (171244) this=0x4306B160 stream=…
[TYMAP-171] #1 r3=0x4306B160 r4=0x0FEFE960
[LDRSH] #1 entry r3=0x0FEFF918 r4=0x00000000
[GATE-FORCE] returned from 171244 path
```

| Conclusão | Evidência |
|-----------|-----------|
| Typemap **existe** no heap natural | `vt=0x5130B8` em `component+0xDC` |
| Walk **mecânico** funciona | TYMAP-171 + LDRSH com force |
| Caller natural **nunca entra** | CMP-ENTER=0 em boots naturais longos |
| SHGX expand ≠ unlock do walk | expand GREEN e ainda TYMAP-171=0 natural |

### VEREDITO: classe **A1** (reforçado pós-WAD)

O elo partido continua: **nada no grafo exercitado chama `func_0032E200`**
(vcall OPD `0x522E70` → `func_00171244`). Não é C (fonte vazia após walk) —
o walk **não corre**. Não é A3 (typemap ausente) — GATE-FORCE acha typemap
vivo. SHGX catalog + WAD BODY são progresso de **asset open/stream**, não
fecham a parede [D] do registry.

### O que **não** fazer

- Celebrar GATE-FORCE / synthetic F85F como aceite
- Forjar N>0 em SHADERSRC / CRC bypass
- Tratar expand SHGX stub como “shaders carregados”

### Próximo fix fiel (candidato)

RE/instrumentar **quem deveria agendar** o component stream com tipo
`0xF85F9B1E` após R_Perm (cadeia acima de `func_0032E200` — nota
`2026-07-21-registry-caller-chain-A1.md`): micro-construtores /
scene load pós-legal-screen, não mais FIOS/WAD open.

