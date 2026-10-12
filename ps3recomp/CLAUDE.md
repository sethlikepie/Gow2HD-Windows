# CLAUDE.md — ps3recomp / GoW2 HD (recompilação estática PS3→Windows/macOS)

Guia operacional destilado das sessões de bring-up do God of War II HD (NPUA80491).
Leia antes de qualquer trabalho. Repositório-irmão: `../gow2-recomp` (repo git separado,
com os artefatos do jogo, launchers e scripts de run — tem CLAUDE.md próprio que aponta pra cá).

## Idioma (obrigatório)

Separar **processamento** de **saída** — isso deixa o raciocínio da IA mais eficiente:

| Camada | Idioma | Exemplos |
|--------|--------|----------|
| **Pensamento / trabalho interno** | **Inglês** | raciocínio, análise, planos técnicos (`PLAN.md`, `RESEARCH.md`, notes de debug), prompts entre agentes, checklists internas |
| **Código e IDs** | **Inglês** | identifiers, comments em hot path, paths, APIs, logs de runtime, nomes de env vars |
| **Output ao usuário** | **pt-BR** | chat, resumos de progresso, UAT, perguntas, mensagens de checkpoint, commits |

- Config GSD: `response_language` = `Portuguese (Brazil)` em [`.planning/config.json`](.planning/config.json) — vale para **texto user-facing**, não para forçar planos/artefatos internos em PT.
- No chat: pt-BR (vocabulário BR: usuário, arquivo, tela, otimização — não o europeu: utilizador, ficheiro, ecrã, optimização).
- **Não** traduzir raciocínio nem reescrever PLAN/RESEARCH só para “ficar em português”; inglês interno é o default desejado.
- Commits: mensagens em **pt-BR** (convenção do repo); corpo técnico do diff permanece em inglês.

## GSD — routing de modelos (obrigatório neste monorepo)

Política completa: [`.planning/GSD-MODEL-ROUTING.md`](.planning/GSD-MODEL-ROUTING.md) · config: [`.planning/config.json`](.planning/config.json).

| Etapa | Motor |
|-------|--------|
| **Plan** (preferido) | **Grok** — `scripts/gsd_plan.sh` |
| **Plan fallback** | **Claude Opus automático** se Grok falhar/timeout/sem PLAN — **sem pedir confirmação** |
| **Execute** | **Claude Opus** — `model_profile: quality` / `models.execution: opus` |
| Execução no Grok | **Não** (`cross_ai_execution: false`) |

Cadeia de plan: Grok → Claude CLI Opus → planner inline nesta sessão Opus.  
Marcador: `.planning/phases/*/.last_plan_engine`.

**Quando o usuário pedir GSD autônomo** (“corre o gsd sozinho”, “autonomo”, “roda o gsd”):

1. Preferir **`/gsd-run-autonomous`** (híbrido + fallback), **não** o stock `/gsd-autonomous`, salvo “stock” / “tudo em Claude” / “sem grok”.
2. Uma fase: **`/gsd-run-phase N`**.
3. Se Grok cair: **fallback Opus já** — não bloquear em pergunta.
4. Foco default do `STATE.md` (v1.1 boot) não muda para v1.2 Go sem pedido explícito.

O stock `/gsd-autonomous` **não** chama Grok — só o playbook híbrido.

## Estrutura de monorepo (games/)

Este repo é o **monorepo**: o motor (runtime/libs/tools) na raiz e cada port em
`games/<titulo>/` — conteúdo real importado via `git subtree` com histórico preservado.

| Port | Pasta | Origem/espelho |
|------|-------|----------------|
| **GoW2 — God of War II HD** (ativo) | [`games/gow2/`](games/gow2/README.md) | [andrebrumdev/gow2-recomp](https://github.com/andrebrumdev/gow2-recomp) |

**Fluxo de trabalho do GoW2:** o checkout FÍSICO de build/run continua em `../gow2-recomp`
(irmão deste repo), porque os dados do jogo (20GB não-versionados: `extracted/`, `EBOOT.ELF`,
`movie_cache/`…) vivem lá ao lado dos scripts. Sincronização com o monorepo:

```bash
# trazer commits feitos no gow2-recomp (via espelho) para o monorepo:
git subtree pull --prefix=games/gow2 https://github.com/andrebrumdev/gow2-recomp.git main
# empurrar mudanças feitas direto em games/gow2 de volta pro espelho:
git subtree push --prefix=games/gow2 https://github.com/andrebrumdev/gow2-recomp.git main
```

Regra: commits do dia-a-dia do port nascem no `../gow2-recomp` (push pro espelho) e são
puxados pro monorepo via `subtree pull` ao fim de cada leva. Novos ports entram como
`games/<titulo>/` pelo mesmo padrão.

## Onde queremos chegar

- **Meta final:** GoW2 100% jogável (boot → menu → gameplay) via recompilação estática, sem emulador.
- **Meta corrente:** pixels de jogo REAIS na tela pelo caminho natural do jogo — registry de
  shaders populado (ICGLdrShader/SHADERSRC N>0), CGOWShader resolvendo, draws/texturas do
  próprio jogo no backend host (**D3D12 no Windows; Metal nativo no macOS/arm64**). Sem
  atalhos cosméticos como aceite de conteúdo.
- **Meta Mac visual (eixo paralelo):** stack Metal M0–M4 — overlay intro VT, draw path,
  MSL, texturas, zero-copy. Índice:
  [`docs/superpowers/plans/2026-07-21-00-macos-metal-stack-INDEX.md`](docs/superpowers/plans/2026-07-21-00-macos-metal-stack-INDEX.md).

---

## Port Apple / macOS — otimização e baixo nível (OBRIGATÓRIO)

Sempre que o trabalho for **port, backend, media, áudio, GPU ou I/O no macOS/arm64**,
otimizar para a plataforma. Não copiar o caminho Windows se existir API nativa melhor.

### Preferir (nativo / baixo nível)

| Área | Usar | Evitar como solução default |
|------|------|------------------------------|
| **GPU / RSX host** | **Metal** (`rsx_metal_backend.m`, PSO, `MTLBuffer` shared, textures) | OpenGL; “só SDL null” como aceite de pixels; depender de MoltenVK se Metal directo for viável |
| **Decode de vídeo (overlay)** | **VideoToolbox** (+ CoreMedia/CoreVideo); depois **IOSurface → MTLTexture** zero-copy | `ffmpeg`/`ffprobe` no PATH; reencode Theora/VP9 do `.m2v` do jogo |
| **Áudio host** | AudioQueue / AudioUnit / CoreAudio | Assumir winmm `PlaySound`; sleep-busy em vez de callbacks |
| **Memória GPU** | Unified memory: `MTLResourceStorageModeShared` onde couber; uma cópia no upload | Staging triplo desnecessário; `memcpy` full-frame se IOSurface bastar |
| **Threads** | pthreads / GCD **medido**; locks finos no present | Spin no giant lock; `pkill -f boot_gow2` |
| **Build** | `build-macos` Ninja arm64; frameworks Apple no link | Puxar deps Win32 (d3d12, winmm) para o target Mac |

### Regras de decisão (checklist mental)

1. **“Isto existe nativo na Apple?”** → se sim, implementar nativo (Metal/VT/CoreAudio), não o port preguiçoso do Win.
2. **“É o hot path (por frame / por AU)?”** → medir; preferir zero-copy, shared buffers, menos syscalls.
3. **“É só cosmético (overlay) ou funil guest (vdec/WAD/shaders)?”** → não misturar aceites; overlay **não** conta como pixels de jogo.
4. **Paridade de contrato, não de implementação:** manter vtable `rsx_backend` / `movie_hle_*`; por baixo, D3D12 vs Metal podem divergir.
5. **Windows não regredir** ao adicionar código Apple (`#ifdef __APPLE__` / arquivos `.m`).

### Planos Metal (executar na ordem do INDEX M*)

- Índice execução: `docs/superpowers/plans/2026-07-21-00-macos-metal-stack-INDEX.md`
- **Backlog / gap D3D12→Metal:** `docs/superpowers/plans/2026-07-21-00-macos-metal-pending-backlog.md`

## Progresso (cadeia de paredes — estado 2026-09-01)

| Parede | Estado | Evidência |
|--------|--------|-----------|
| [A] Intro/FSM do movie player | ✅ VENCIDA | Intro toca e progride nos 2 caminhos (FORCE e EOS natural); arm de EOS em qualquer estado ativo, desacoplado do trace |
| [B] WADs abrem (R_LglScA/R_PermA) | ✅ VENCIDA | `[movieio] open` nos dois caminhos |
| [C] Stream/consumo do R_PermA (20MB) | ✅ VENCIDA (host); 🟡 natural REABERTO (E258): o R_PermA abre mas o loader deref pointers TAGGED `0x84000017` -> a CHUNK ARENA (alocador, FUN_263040) corrompe (`FREELIST-TAG-GUARD abort` 0 no e130, 8 no HEAD) = o "pool de handles" REABERTO. NAO e' o FIOS op freelist (E257 retractado) nem o fallthrough 2550C8 (intacto). Walk CONVERGIU (E263): func_002ACBE8 e' um LOOKUP por chave (nao free-list); devolve 0 = chave NAO registada. Toda a corrupcao do pool e' SINTOMA de a R_PermA natural nao fazer stream (early-cancel a ~14 blocos, so' o header). RAIZ UNICA: o DONE-poll do guest (func_002B42B4) cancela a leitura do R_PermA cedo. AVANCO (E275): com PS3_WAD_HOST_READ=1 + PS3_FIOS_STREAM_PUMP=1 + PS3_F2B_HOST_FILL=1 o F2B-STREAM-FILL enche o input stream do estado 22 (0x4007FCD0) com os dados do R_PermA do cache, e o LOADER PROCESSA os primeiros 256KB (28 LOOKUP/ATTACH/MDLX) -- 1a vez que consome dados reais do WAD. Aberto: o ring so' enche 256KB (o reader func_002B9C80 nao avanca o cursor stream+8 que o fill segue -> avail=cap -> sem refill -> crash). BREAKTHROUGH (E287): com o R_PermA entregue (E275) a CONSTRUCAO DO NIVEL EXECUTA -- o loader consome 19 records validos do R_PermA (b@cur=0xXX000080, size 0x80, cursor a avancar) e crasha ao 19.o (so' 2.6KB de 256KB usados, NAO e' falta de dados nem framing -- o size=0 do E285 era o campo errado). Stack: func_002B0DB0 (dispatcher do registry: puVar1[type*4]) -> func_0039E794 (handler faz lookup de resource) -> indirect call CRASH: o handler/resource do record 19 NAO esta' na tabela (registry incompleto). E' o mecanismo de [D]. MEDIDO (E292): 5 constructs de SHELL type-15 (CGOWShader, func_0039E794) SUCEDEM (post#1..#5, idx 0x0C/07/08/06/1B); o 6.o (idx 0x12, this=0x400D7888, vtable 0x517688 VALIDO) crasha no metodo do construct (indirect call a 0x4BFFFE34, cauda do buffer HOSTRES). O sistema de construct de shaders EXECUTA e completa 5x -- a prova mais forte de que a construcao do nivel popula o sistema de shaders; falta o 6.o shell (idx 0x12) nao crashar. Proximo: comparar o construct/resource de idx 0x12 vs 0x0C. [D] ainda nao (registry vazio: o crash trava antes de completar) |
| [C2] Texturas do WAD decodificadas | 🟡 EM PROVA | Deswizzle Morton + bind_texture real 128x128/256x256 fmt=0x85 (`frame_wad_tex_*.png` no gow2-recomp); legal screen DXT1 via gzip HLE |
| [D] Registry de shaders | 🟢 AVANCO MEDIDO (E353z/E354): **18 shells CGOWShader construidos (era 5)** + **R_PermA entregue COMPLETO (20169344/20169344)** + **0 crashes**. Cadeia: PS3_MOVIE_DONE_MS=9000 completa a intro (st620 3->11->0, eos armado) -> F2B entrega os 20MB -> o UNDERFLOW do scope stack era o GATE da construcao (um so' clamp diagnostico gated PS3_SCOPE_CLAMP libertou +13 shells). Falta: 18->27 shells, 1->13 arenas WAD_R_, LDRSH=0 (compiles de nivel nao disparam), front-end fica idle em st620=0. O clamp e' DIAGNOSTICO gated OFF (mascara o desequilibrio enter/leave; fix real = o desequilibrio, payoff medido +13 shells). needs-level REFUTADO -- bug NOSSO, alcancavel SEM [G] (E353, oraculo front-end AUTONOMO). O experimento decisor CORREU (headless, nao precisa GUI: tools/rpcs3_probe/oracle_frontend_rem.py, EBOOT_intro.BIN, bp func_002BA76C, le rem no global fixo 0x0053FF4C->type_sys=0x869570). MEDIDO 2x, consola durante a intro (t~22s): container R_PermA abre (rem=0x133C280, state 2), depois **state 3 com rem a DESCER continuamente** (0x133C280->0x00FFC280, 338 hits, passos de 128KB) e **body1D4!=0** (0xFC830,0xDC830...) = body-load REAL no front-end (segue ate outros WADs). NOS (E338/E339): paramos em state 2 com body1D4=0. Divergencia = transicao state 2->3: o parse do header de record da' body1D4=0 (devia dar 0xFC830). REFUTA needs-level (E350/E351) e a duvida de E352; CONFIRMA a tese E338. Proximo cirurgico: origem de body1D4 (ts+0x1D4) no header, porque o nosso da' 0 -- bp consola em func_002BA9BC (setter state=3) e no ramo de conclusao state 2 (func_002BA94C->func_002BA960 vtable+0x48). [D] SEM [G] no caminho. FUNIL POR ELIMINACAO TESTADA (E355-E366, 19 correccoes): NAO e' timing da intro (E362 refutado), NAO e' ficheiro truncado (E364: o psarc tem os MESMOS 3072 B de r_lglsca), NAO e' registo/lookup (E360: a consola falha os MESMOS lookups, 0/20), NAO e' hash (E358: 8/8 exactas vs consola), NAO e' o remap do fo (E366: F2B-HOLD nao disparou). ABERTO: o loader consome os 3 KB do R_LglScA e produz 0 lookups '_R_LglScA' enquanto a consola produz 19 do MESMO ficheiro -- nunca abre o LglScA como container (WADLD-SM state=1 rem=0), so' processa o R_Perm. Crash fixo (E324), spu6 (E330). ESTADO E353z-E381 (25 correccoes medidas): a via LEGITIMA (sem o band-aid PS3_F2B_WAKE, que tinha semantica invertida e prendia o loader num spin -- E371) ticka o loader 80x com estados 1->2->3 e CONSOME ~13 MB do R_PermA (body-load real, rem 0x133C280->0x69E9F0), e morre em func_002F0AAC com *(obj+0xC)==NULL (descritor de handler por preencher). ORACULO confirma (E381): a consola no MESMO ponto tem obj=0x00C20CCC com +0xC=0x00C20504 -> code=0x00C13AAC VALIDO. Alvo final: quem escreve obj+0xC. Refutados por medicao nesta leva: registo/lookup (a consola falha os mesmos lookups 0/20), hash (8/8 exactas), ficheiro truncado (o psarc tem os mesmos 3072 B de r_lglsca), timing da intro, remap do fo, e a 'lista de callbacks'. |size19), enquanto na consola e' limpa. E' a corrupcao pool-de-handles de §4a; a tabela de objetos e' a vitima. ~25 hipoteses refutadas eram sintomas. Fix: walk da free-list da arena (func_00263040/func_00263178) vs consola. Avanco: E275=5 shells. | Entry do ICGLdr (`[LDRSH]`) provado pós-WAD com stream sintético mínimo, SEM CRC bypass; o nested walk NATURAL do typemap segue aberto. **Desde o M9 (E146) só é alcançável se a intro acabar naturalmente** — o 2.º filme já não passa pela ponte EOS e depende do relógio de áudio (mixer SCREAM, ver [G]); medido E174–E177 + bissecção e130/e146/e153 (ledger E147–E173 §4c) **E385 (2026-09-04): o handler NULL de `func_002F0AAC` é o `callback` do `CellGcmContextData` (libgcm_sys; identificado pelo mapa de memória do log do RPCS3), que o nosso `cellGcmSetupContext` deixa a 0 (E66). `PS3_GCM_CB=1` A/B: 5→18 shells, 0 ICALL-BAD, sem crash. Plateau em 18 (consola: contínuo). `LDRSH`=0.** **E387-E394 (2026-09-04, cadeia fechada por medição, consola vs nós):** o plateau em 18 é um SPIN no walk do registry (`func_002ACC30`) porque os lookups do loader correm com a tabela a NULL (`map=4`): o registry é o slot da pilha de scope (`func_002AAB64`), vazio porque falta o ENTER/NEWREG da criação da arena do LglScA (`func_002BBA64` vê `size=-1`; a consola vê `0x7A120`); o record type-21 do LglScA nunca chega ao leitor porque o **caminho síncrono de `func_002BAD10` (flag 0x200, chamado em 0x41F68) não corre o loop de ticks em nós** (consola: open `r3=1`, `ts+0x74=0xC00`, loop em 0x2BAE14). Próximo: por que fragmento sai o nosso `func_002BAD10` após o open (B20). Ferramentas: `oracle_scopeidx/scope_seq/enters/t21/bad10.py`, `watch_run.sh`, `sample <pid>`. **E401/E402 (2026-09-04) — CADEIA FECHADA E FIX PROVADO:** a raiz era um paliativo NOSSO (`FIOS-42B4-CANCEL-YIELD`: yield + usleep 50 ms a seguir ao delete da op no DONE, `ppu_recomp_005.cpp`, não gated) que deixava a thread FIOS zerar `op+4` antes da cauda do open síncrono (a consola lê primeiro: `oracle_optail.py`). Gated OFF por `patch_e401_fios_done_yield_gate.py` (`PS3_FIOS_DONE_YIELD=1` repõe): open síncrono devolve 1 em 5/5 corridas com intro completa, o LglScA é processado, arena criada, **registry REAL (idx 14, 6 091 lookups, zero no endereço 4), 70 shells**. Parede seguinte: 71.º shell esgota a arena do LglScA (0x7A120) → `abort()`; a consola tem arenas maiores de contentores que carrega por `func_002BAD10` noutros sítios do driver (lr 0x42F0C/0x42AA4). `LDRSH`=0.** |
| [E] Pixels de jogo naturais | ⬜ BLOQUEADO por [F]/[G] | Depende de D natural + draws do jogo |
| [F] O frame 1 virar frame N | ✅ **VENCIDA (E90, 2026-09-01)** | Causa: o **preenchimento host do ring do WAD** (F2B fill/pump/ALIGN, Julho) rebobinava o R_PermA no fim do stream (`F2B-STREAM-ALIGN`), o leitor lia lixo (`F46F`) e um **segundo `0x06`** despejava o R_PermA → conjunto A a −1 → #042 sem produto. Provado com o oráculo RPCS3 destravado (`tools/rpcs3_probe/`, ver E90): no console há um só `0x06` e o #038 acaba com o conjunto A a 0. Fix: `patch_e90_f2b_natural_gate.py` — caminho natural (chunks `FUN_002b9f00`→`AREAD-HLE`) por default; `PS3_F2B_HOST_FILL=1` repõe o legado só para A/B. Medido: 3/3 corridas ON → **frame 10 000–10 500**, VAZIO/ICALL-BAD=0, sequência de registos = oráculo; controlo OFF → frame=1 |
| [G] O estado da aplicação transitar | 🟡 **LOADER SEGUE O CONSOLE ATÉ AO ESTADO 22 (E129, 2026-09-02)** | Três defeitos NOSSOS por baixo do spin de E110 §22, todos com fix natural: (1) **lifter**: fragmento que acaba em `if (...) goto` ficava sem trampolim de fallthrough (`_last_line_is_terminator`), 31 sítios, um deles em `FUN_0024d0f4` → `r1` a derivar 0x80 por chamada → entidade auto-ligada → chave `0x512000` → spin; (2) **`lfq_host_inflate.c`** lia o event flag como destino e escrevia 64 KB por cima do scheduler FIOS — o dearchiver "rejeitava" por isso (F2B nasceu daí); (3) **`sys_rwlock`** bloqueava com o giant lock (deadlock com a thread do FIOS). Ledger: [`docs/re_sessions/2026-09-02-E124-E139-fallthrough-condicional-inflate-e-rwlock.md`](docs/re_sessions/2026-09-02-E124-E139-fallthrough-condicional-inflate-e-rwlock.md). M9 feito (E140–E146): `cellVdec` descodifica os AUs reais via VideoToolbox, PICOUT por imagem, `CellVdecPicItem` real. Aberto: o relógio do vídeo é corrigido pelo **tempo do áudio** (`FUN_0045b780`), que em nós fica em 0 porque a posição das vozes do Scream nunca avança. Andado até ao produtor (E147–E173): (a) jump table truncada no lifter (E162, corrigido); (b) o mixer é uma **workload SPURS** cujo `_cellSpursWorkloadAttributeInitialize` (0xEFEB2679) estava por resolver (E168, corrigido) — agora o PM do SCREAM chega ao dispatcher como `MISS fp=0xCEDB9A67A0C3A305` (11 520 B em `0x4FD980`): falta liftá-lo (`spu6`) e dar-lhe o contexto de kernel SPURS para PMs crus. Ledger: `docs/re_sessions/2026-09-02-E147-E162-*.md` §4b |

Histórico completo: `docs/gow2-asset-pipeline-map.md` (a cadeia de causa raiz inteira),
`docs/gow2-recomp-notes.md` (paredes A–E + matriz de aceite), `docs/superpowers/plans/*.md`
(planos executados com checkboxes reais), `.superpowers/sdd/progress.md` (ledger de execução).

### O megaprojeto vencido (lição de referência)
O "pool de handles" que assombrava o projeto desde a leva 10 caiu em 6 rodadas de
debugging sistemático (T3→T3f do plano 3): cada rodada refutou hipóteses com medição
e estreitou o funil — deref taggeado → alocador fiel (não era o lifter) → free-list
esgota → cursor sem magic vindo do escopo → grow herda arena do objeto → **fallthrough
cross-fragment errado** (`func_002550C8` caía em `func_002550E8` em vez de `func_00255178`).
Fix de 1 linha + script idempotente. Moral: o bug de meses era rastreável; o método venceu.

## O que NÃO fazer (regras absolutas)

1. **NUNCA commitar dados de jogo** (copyright): `EBOOT.ELF/BIN`, `*.psarc`, `*.self`,
   `extracted/`, `movie_cache/`, `spu_images/`, `*.m2v`, `*.wav`, `*.wad_ps3`.
   Os `.gitignore` cobrem — mas NUNCA use `git add -A`/`git add .`; sempre arquivos explícitos.
2. **Push SÓ para o fork pessoal** (`andrebrumdev`): neste repo o remote é `personal`
   (ps3recomp) e no gow2-recomp é `origin` (gow2-recomp). **NUNCA push para sp00nznet.**
3. **Commits em português brasileiro (pt-BR), SEM co-autoria** (nada de Co-Authored-By).
4. **NUNCA forjar resultados.** Aceites distinguem sempre: provado **in-boot** vs
   **offline/unit** vs **não-exercitado**. Não escrever "funciona/renderiza" sem observação.
5. **Proibidos como "fix":** CRC bypass, stub de combination, estampar magic à mão,
   null-check cosmético, mascarar guard, `PS3_SHADER_DEMO` como aceite de conteúdo.
   Esses são no máximo ferramentas de diagnóstico, sempre gated e declaradas.
6. **Probes/experimentos sempre gated por env var, OFF por default** (no-op no baseline).
7. **Não commitar working tree alheio:** arquivos `M` que outra sessão deixou
   (ex.: `cellGcmSys.c` em certas janelas) ficam fora dos seus commits.
8. Fix fiel ao comportamento do CELL/PPC original — nunca "consertar" divergindo do console.
9. **Vídeo (cellVdec/VT):** imagens SEMPRE na ordem de exibição (B-frames reordenados em `cellVdec_vt.m`); fila cheia = BUSY, nunca descartar AU; hook CE03C e overlay host OFF por default. Guarda: `tools/vdec_order_check/`. Histórico: `docs/re_sessions/2026-09-23-intro-e-cutscenes-video.md`.

## O que PODEMOS fazer

- Instrumentação agressiva (gated). HLE fiel quando o caminho real é inalcançável —
  de preferência CHAMANDO o próprio código guest (ex.: invocar o init real), não simulando.
- Editar os `ppu_recomp_XXX.cpp` locais (gitignored) para probes/fixes — mas todo fix
  neles vira **script idempotente commitado** (`patch_*.py`, ex. `patch_fallthrough_2550c8.py`)
  para sobreviver a re-lift.
- Remover paliativos antigos NOSSOS (circuit-breakers) quando a evidência provar que
  atrapalham — com prova de não-regressão do baseline.
- **No Mac:** investir em Metal/VideoToolbox/CoreAudio de verdade (planos M0–M4), não
  em shims lentos “que compilam nos três OS” quando o hot path é só Apple.

## Como trabalhar (metodologia que venceu)

- **Subagent-driven** para planos: brief por task com âncoras `arquivo:linha` VERIFICADAS
  antes de despachar (nunca confiar em linha de plano antigo); 1 implementer por vez;
  review por task; achados Minor no ledger; revisão final de branch.
  Ledger: `.superpowers/sdd/progress.md` — sobrevive a compaction de contexto; atualize-o
  a cada task e confie nele + `git log` após perda de contexto.
- **Debugging sistemático:** evidência → hipóteses ordenadas → experimento DISCRIMINADOR
  (ex.: PS3_COMMIT_HIGH refutou "falta de commit" em 1 run) → refutações medidas → fix mínimo.
  Timebox ~2h por rodada; **diagnóstico rigoroso é entrega válida** (nunca fix apressado).
- **Suspeitar de nós mesmos primeiro:** auditar nossos próprios patches/breakers antes
  de culpar o jogo (barato e já quase foi a causa).
- Ao achar bug do lifter, verificar se é **sistemático** e auditar o padrão no resto.
  **`g_trampoline_fn` com alvo para trás — REFORMULADO a 2026-09-01 (E83), não fechado:**
  - a metade do **fallthrough auto-anexado** está **refutada por construção**: os dois
    caminhos de resolução em `tools/ppu_lifter.py:3645-3654` dão sempre `≥ end > start`;
  - a metade que **sobra**: trampolim **incondicional em posição de cauda** com alvo
    para trás, que é um `b` do jogo entre fragmentos da MESMA função. **Medida a
    2026-09-01: 15 583 sítios** (de 17 294 com alvo para trás; 891 são tail-calls entre
    funções diferentes). Com essa população, **a forma sozinha não é defeito** — o boot
    não chegaria ao `frame=1`. Os fragmentos partilham o mesmo `ppu_context*`, logo um
    trampolim entre eles **não altera registadores**;
  - **não confundir com a parede [F]:** o E83 chegou a apontar
    `func_00264AF8 → func_002649FC` como caso desta classe e **retratou-o no mesmo dia**
    (E83 §7). Ali o `r31` está errado porque **o argumento `r3` está errado** —
    `func_00264988` faz `r31 := r3` — e o back-edge é só o caminho;
  - o que **fica por caracterizar**: se esta forma alguma vez produz defeito. Hoje são
    15 583 sítios **sem um único caso provado**. Quem pegar nisto começa por procurar um
    fragmento cujo `ctx` NÃO seja partilhado, ou um prólogo de fragmento que restaure
    callee-saves lidos da stack fora do sítio certo (`func_002649FC` tem essa forma).
- **Jump table truncada (E162, 2026-09-02, corrigido):** `discover_jump_tables` lia o limite do
  `switch` no último `cmp[l]wi` das 30 instruções antes do `bctr` **atravessando `b` incondicionais**
  — o `cmp` de um bloco irmão dava `count=0`, a tabela de 8 ficava com 1 case e os outros índices
  caíam em `ps3_indirect_tail` → `[ICALL-BAD]` → retorno ao chamador **sem epílogo** (`r1` com o
  frame do callee por desfazer). 8 de 157 dispatchers do GoW2. Sintoma típico: `[CSAVE]` com `r1`
  a descer o tamanho do frame do callee. Ledger: `docs/re_sessions/2026-09-02-E147-E162-*.md`.
- `ra` do host stack é retorno de trampolim (varia entre runs) — rastrear callers por
  fluxo estático + `nm`, não pelo stack.

## Integrar melhorias do projeto original (sp00nznet/ps3recomp)

Runbook completo (remotes, politica de conflito por arquivo, portabilidade macOS
pos-merge, smoke de nao-regressao): skill **`integrar-upstream`**.
Regra que nao muda: push SO para o fork pessoal; nunca `git push upstream`.

## Build & run (essencial)

- Exe: `gow2-recomp/recomp_mid_v2/boot_v2_new.exe ../EBOOT.ELF` (rodar de `recomp_mid_v2`).
- Rebuild lib runtime (~1.5min): `gcc -std=c11 -O0 -c $INC ../../ps3recomp/libs/.../x.c -o lib_x.o`
  + relink (ver `build_crc.sh` para o comando completo com `-Wl,--stack,33554432` e base fixa).
- Rebuild chunk guest (`ppu_recomp_XXX.cpp`, ~5-8min): `g++ -std=c++20 -O0`.
- **`exit 124` é NORMAL** (boot roda em loop de render até timeout). `taskkill //F //IM
  boot_v2_new.exe` antes/depois de cada run. Flakiness conhecida: "vm commit (sys_memory
  window) failed" ocasional → re-rodar, não perseguir.
- Env recipe canônico (intro→WADs, validado): `PS3_VFS_ROOT=<...>/gow2-recomp/extracted/USRDIR
  PS3_VM_LOW_MB=512 PS3_VM_STACK_MB=64 PS3_CELLSYS_REORDER=1 PS3_FIX_TBLSIZE=1 PS3_MOVIE_IO=1
  PS3_MOVIE_CACHE=../movie_cache PS3_MOVIE_EOS=1 PS3_VDEC_ASYNC=1 [PS3_VDEC_FORCE_SEQDONE_MS=8000]
  [PS3_MOVIE_HLE=1 PS3_RSX_BACKEND=d3d12 PS3_RSX_FIFO=1 PS3_PAD_AUTOSTART=1] [PS3_SPU1=1]`.
  **`PS3_VDEC_ASYNC=1` é obrigatório** (o default voltou a sync no f811ee3; o FORCE só roda async).
- Scripts prontos (gow2-recomp/recomp_mid_v2): `bt_intro_wads.sh` (FORCE→WADs), `bt_visual_combo.sh`
  (visual+EOS), `smoke_asset_pipeline.sh` (métricas de raiz), `run_basecase.sh` (heap),
  `rodar_gow2_intro_skip.cmd` (launcher clicável). Smokes E2E: `smoke_rsx_spu.sh`, `smoke_intro_to_rsx.sh`.
- Janela D3D12 abre na sessão de quem lança o processo — para o usuário VER, ele mesmo
  roda o launcher (`!` no prompt ou duplo-clique), não a sessão do agente.

## Próximos passos (em ordem)

0. **Relógio de áudio → mixer do SCREAM (E173):** liftar o policy module `0x4FD980` (fp `0xCEDB9A67A0C3A305`, base LS `0x3000`) como `spu6`, dar ao dispatcher um caminho para PMs crus (LS `0x3000`, contexto do kernel SPURS, `data` em r4) e ver que imagem o PM carrega por DMA de `0x886280`; sem isto a posição das vozes fica 0 e o filme não passa da imagem 1 sem `PS3_MOVIE_EOS`. Cuidado: o tick E156 (2 ms) ocupa os 2 workers com as tasks do taskset (E172).
0a. **Pós-intro (E178–E194):** com a ponte gated `PS3_MOVIE_VT_REPLAY=1` a intro acaba e a reserva GETLLAR/PUTLLC (E190) tirou o stall da LFQueue do SCREAM; falta (i) **o open natural pós-intro devolve `0x80010708`**: o scheduler (`FUN_0030ce84`) cancela ops com `op+0x30&0x200`; os nossos yields injectados no FIOS (`PS3_FIOS_YIELDS=0` no lift resolve o re-open do m2v) e o **lwmutex stub sem exclusão mútua** (`ppu_sysprx.cpp`; o real existe gated `PS3_LWMUTEX_REAL=1` mas pára o boot antes da intro porque o task-lib SPU do decoder entra no caminho de espera e precisa dos re-lifts spu1 `--extra-funcs 0x3128,0x4070,0x3308,0x9300` e spu2 `0x96F8` (feito, §4i) e, a seguir, o arranque em modo real já passa a intro depois do fix do `recursive_count` no `lwcond_wait` (E233) e o giant lock tem preempção temporal (E229, default ON); o cancel dos WADs vem, verificado por watch de `op+0x44` (E238), de `FUN_0030fbf8` (thread fios scheduler) → `FUN_0030ed68` → `FUN_0030cd48` (o op do WAD cai na lista de cancelamento `uVar15` da passagem de dispatch); a causa é `op+0x30=0x300` no op do WAD (marcador "completo") escrito FORA do caminho de store guest (E240: watch cap 100k não vê a escrita) — (RETRACTADO E245: o `0x80010708` é estado interno normal do FIOS — a consola também o carimba e os WADs carregam; o sinal real é o **stream do R_PermA**, que corria em `e130b` cedo nesta sessão — `bytes_read=20169344`, loader `[LDRSTATE]=18607` — e regrediu num commit de runtime desde então. Bisect entre `e130b` e HEAD com critério `bytes_read==20169344 && LDRSTATE>0`.) Contexto anterior (agora subordinado): uma corrida de wakeup (E244): o open do WAD COMPLETA (`op+0x30=0x300`), mas o submetora dorme em `lwcond opWait` e não consome a completação antes de a limpeza do scheduler (`FUN_0030fbf8→FUN_0030ed68→FUN_0030cd48`) cancelar a op sem dono e re-carimbar `0x80010708`; retractadas as hipóteses de corrupção de §4o/§4p (todos os escritores host instrumentados: zero hits) — E203–E244 §4f–§4q; o fix é sinalizar `opWait` na completação / não cancelar op completada sem dono há menos de um tick; é o pré-requisito do open dos WADs); por isso os ELF SPU do escalonador do frontend são despachados **a zeros** (E195) e o `schedul` nunca sinaliza; (ii) a corrupção intermitente `ctr=1 r2=0` a seguir a imports (corrida); só depois o loader/`[LDRSTATE]` volta a correr e [D] fica alcançável. Ledger §4d.
0b. **Parede [G]** (estado da aplicação preso em `0x0052F3F0`): usar o oráculo RPCS3 destravado (E90 §1) para ler o próximo estado/transição no console e comparar com `PS3_TRACE_STATE=a` nosso. Receita do oráculo: `tools/rpcs3_probe/` (ELF com intro saltada em `USRDIR/EBOOT_skipintro.BIN`, interpretador, `vCont`).
0. **Intro natural (M9):** `cellVdec` a descodificar os AUs que o guest entrega (VideoToolbox MPEG-2 → Metal) e `SEQDONE` no `cellVdecEndSeq` do guest, para retirar o `PS3_MOVIE_EOS`; decidir um só consumidor da fila edgezlib (host vs task SPU). Ver E124–E139 §9.
1. **Task 4 natural:** nested walk do typemap pós-R_PermA → ICGLdrShader invocado pelo
   caminho REAL (sem stream sintético); SHADERSRC com N>0 e fonte não-vazia.
2. **Task 5:** primeiro FP/textura de conteúdo no `set_shader`/draw natural (aceite: hash
   novo de FP dumpado ≠ demo, ou bind_texture de buffer pós-stream, correlacionado).
3. **Auditoria sistemática do fallthrough cross-fragment** (padrão do fix 2550C8) no lift inteiro.
4. Follow-ups do ledger (`.superpowers/sdd/progress.md`): bloqueadores de promoção
   spu2/3 a default, byte-swap 64/128bpp, constantes 0xAD/0xAE, suite SPU com stubs.
