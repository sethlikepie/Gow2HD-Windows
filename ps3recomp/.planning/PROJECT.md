# PROJECT — ps3recomp

**Criado:** 2026-07-26 (a partir de `/gsd-ingest-docs`, modo `new-project-from-ingest`)
**Fontes:** `.planning/intel/` (44 documentos classificados), `.planning/INGEST-CONFLICTS.md`,
`.planning/codebase/` (mapa verificado do código, somente leitura)
**Idioma dos artefatos de planejamento:** português do Brasil. Identificadores técnicos,
caminhos, símbolos, variáveis de ambiente e IDs de requisito ficam verbatim em inglês.

---

## Visão geral

`ps3recomp` é um **recompilador estático ahead-of-time**: traduz binários de jogos de
PlayStation 3 (Cell/PPC64 + SPU) para código C/C++ nativo que um compilador de host
comum transforma num executável. Não há emulação de CPU em tempo de execução — o código
recompilado é ligado contra a biblioteca de runtime `ps3recomp_runtime`, que fornece os
serviços de sistema do PS3 (HLE de módulos, syscalls LV2, memória guest, RSX, SPURS).

O repositório é um **monorepo**: o motor na raiz (`runtime/`, `libs/`, `tools/`) e cada
port em `games/<titulo>/`. O port ativo é **God of War II HD (NPUA80491)**, cujo trabalho
diário acontece fisicamente no checkout irmão `../gow2-recomp` (onde vivem os ~20 GB de
dados de jogo não versionados) e é sincronizado por `git subtree`.

## Valor central

**Um jogo de PS3 vira um executável nativo que roda sem emulador — e a recompilação é
reproduzível a partir do binário original, não de edições manuais no código gerado.**

Tudo o mais é consequência: fidelidade do lifter, HLE dos módulos, backend gráfico
nativo. Se o resultado só existe porque alguém editou à mão o `.cpp` gerado, o projeto
não entregou o seu valor central — entregou um artefato irreprodutível.

## Plataformas-alvo

| Plataforma | Papel | Estado verificado (`.planning/codebase/STACK.md`) |
|---|---|---|
| **macOS/arm64** | **primária, ativa** — backend Metal nativo | única plataforma exercitada ponta a ponta; único job de CI bloqueante |
| **Windows (MSVC)** | secundária, suportada — backend D3D12 | job de CI com `continue-on-error: true` (informativo) |
| **Linux** | apenas build de CI | job com `continue-on-error: true`; `README.md` diz "not yet verified green" |

Regra derivada (G4): macOS/arm64 é onde tudo se mede; **Windows não pode regredir**.
Proibido fora de `#ifdef _WIN32`: `<intrin.h>`, `Sleep(`, `__declspec`, `GetModuleHandleA`.

## Escopo do Marco 1

**RDY-0 — desbloquear o re-lift.** Decidido pelo usuário no portão de ingest.

Métrica de sucesso: **o build volta a executar o lifter**, e os `patch_*.py` versionados
aplicam-se limpos sobre uma saída de lift recém-gerada.

Justificativa aceita: o intel classifica isto como pré-requisito duro — enquanto estiver
aberto, **qualquer correção do lado do lifter tem efeito ZERO no binário `boot_gow2`
entregue**, o que torna RDY-2/RDY-3/RDY-4 e todas as trilhas a jusante inertes.

Confirmação medida nesta sessão (não apenas afirmação do plano):

- `games/gow2/build_macos.sh:47` aborta com `no lift output in $LIFT -- run ppu_lifter.py first`
  — o script de build **consome** um lift pré-existente e nunca o regenera.
- `games/gow2/build_macos.sh:92,106,192` compilam `ppu_stubs.cpp`, mas `grep ppu_stubs tools/ppu_lifter.py`
  não retorna nada — o lifter atual **não emite** esse arquivo.
- **73** scripts `patch_*.py` rastreados em `games/gow2/recomp_mid_v2/` (verificado por
  `ls` e cruzado com `git ls-files` no mapa do código) e nenhum gate automatizado prova
  que ainda se aplicam.

## Fora de escopo do Marco 1

- A parede ativa do GoW2 (Gate A, TYPE15 f4 writers, re-arm do present) — dona é
  `docs/superpowers/plans/2026-07-23-00-gow2-playable-master-INDEX.md`.
- Qualquer promessa de pixels de jogo, menu ou jogabilidade. O aceite de RDY-0 é
  **headless** e declara explicitamente que Gate A continua **RED** como valor esperado.
- `R_PermA` não entra no aceite (não é observável na receita M0 headless).

---

## Restrições

### Globais do projeto (SPEC `2026-07-25-fase1-desmascarar-memoria-design.md`, G1–G8)

| ID | Restrição |
|---|---|
| G1 | Formato canônico de trace: `[PS3T] <tb> \| <thr> \| <fn> \| <subsys> \| <event> \| <data>`; separador exatamente ` \| `; uma linha = uma chamada de escrita; destino `stderr`; sanitização centralizada em `ps3_trace_emit`. |
| G2 | Sondas (probes) são **OFF** por padrão; correções de bug são **ON** por padrão e a exceção deliberada é declarada. Com os gates desligados: zero bytes emitidos, zero mudança de comportamento. |
| G3 | A camada de observação não inverte controle: quem decide engolir continua decidindo. |
| G4 | macOS/arm64 primário; Windows não regride; tokens Win32 proibidos fora de `#ifdef _WIN32`. |
| G5 | **Honestidade do aceite**: toda afirmação declara o nível de evidência — `in-boot`, `offline-unit` ou `não-exercitado`. Superdeclarar é defeito. |
| G6 | Protocolo de medição: `PS3_NO_RSX=1` **e** `PS3_RSX_BACKEND=trace`; matar por PID (TERM, depois `-9`), nunca `pkill -f boot_gow2`; logs fora dos dois repositórios; `PS3_ENGINE_ROOT` obrigatório ao construir `boot_gow2`. |
| G7 | Commits em português, sem `Co-Authored-By`; `git add` com caminhos explícitos, nunca `-A`; nunca dados de jogo nem `runtime/spu/tests/out_*/spu_recomp.{c,h}`. |
| G8 | Fronteira de repositório declarada por entregável; nada de commits cruzados não anunciados. |

### Regras absolutas herdadas do `CLAUDE.md`

1. Nunca commitar dados de jogo (`EBOOT.ELF/BIN`, `*.psarc`, `*.self`, `extracted/`,
   `movie_cache/`, `spu_images/`, `*.m2v`, `*.wav`, `*.wad_ps3`).
2. Push apenas para o fork pessoal (`personal`/`origin`), nunca para `sp00nznet`.
3. Commits em português, sem coautoria.
4. **Nunca forjar resultados.** Aceite distingue `in-boot` de `offline/unit` de `não-exercitado`.
5. Proibidos como "fix": CRC bypass, stub de combination, estampar magic à mão, null-check
   cosmético, mascarar guard, `PS3_SHADER_DEMO` como aceite de conteúdo.
6. Sondas/experimentos sempre gated por env var, OFF por padrão.
7. Não commitar working tree alheio.
8. Correção fiel ao comportamento do CELL/PPC original.

### Lacunas declaradas que limitam o que qualquer medição pode afirmar

- **~2110 escritas diretas em `vm_base`** emitidas pelo lifter (`stvx/stvebx/stvehx/stvewx`
  1184, `stwbrx/sthbrx` 922, `dcbz` 4) contornam o funil instrumentado ⇒ contagens de
  `SWALLOWED`/`OOB` são **limite inferior**. Fechar exige mudar o lifter e re-liftar.
- **Duas máscaras `type15_*`** (`ps3_type15_block_stomp`, `type15_pin_free`) vivem em
  `gow2-recomp/host_gow2_factory.cpp` e não estão instrumentadas.
- **A mina do re-lift**: o lifter atual emite `ppu_res_lwarx/stwcx/ldarx/stdcx` como
  `static inline` sobre `vm_base` **sem** `vm_oob` e **sem** `ps3_mem_oob_report`; o lift
  em produção ainda usa os `ppu_lwarx` instrumentados (5277 chamadas). Um re-lift hoje
  apagaria silenciosamente a instrumentação da Fase 0 dos 4 atômicos. **Esta é
  exatamente a mina que o Marco 1 tem de desarmar ou declarar.**
- **Segunda superfície de acessores** em `runtime/ppu/ppu_memory.h:60-172` (cópia
  `static inline` que só checa `ppu_guest_range_committed()` e não emite nada;
  `vm_memcpy_from/to` e `vm_memset` sem guard). Risco declarado baixo.

---

<decisions>

## Decisões arquiteturais

**NENHUMA decisão está travada (locked).** As 19 decisões extraídas dos 3 ADRs carregam
`status: proposto` porque nenhum ADR do conjunto possui campo `Status: Accepted`. Elas
são enquadramento arquitetural forte, não decisões imóveis. Promover qualquer uma a
`locked` exige uma decisão explícita do autor do projeto.

### ADR-ARCH-1 — Recompilação estática AOT, não emulação em runtime
- fonte: `docs/ARCHITECTURE.md` · status: **proposto**
- Transformar o ELF do PS3 em C/C++ nativo antes da execução, num pipeline de cinco
  estágios (carga/análise do ELF → desassembly PPU → análise de fluxo de controle →
  geração de código → compilação pelo compilador do host), ligado ao runtime. Não há
  interpretação de código PPU em tempo de execução.

### ADR-ARCH-2 — Tradução plana guest→host sobre 4 GB mapeados
- fonte: `docs/ARCHITECTURE.md` · status: **proposto**
- `host_ptr = vm::g_base + guest_addr`; `vm::g_base` definido na inicialização por uma
  região grande de `mmap`/`VirtualAlloc`. Jogos usam endereços efetivos de 32 bits.

### ADR-ARCH-3 — Conversão de endianness só na fronteira de memória
- fonte: `docs/ARCHITECTURE.md` · status: **proposto**
- Todo acesso à memória passa por `be_t<T>` / `vm::read<T>` / `vm::write<T>`. Cálculo em
  registradores permanece na ordem nativa do host.

### ADR-ARCH-4 — Mapa fixo de regiões de memória guest
- fonte: `docs/ARCHITECTURE.md` · status: **proposto**
- `0x00010000-0x0FFFFFFF` texto+dados do ELF; `0x10000000-0x1FFFFFFF` PRX;
  `0x20000000-0x2FFFFFFF` stack/TLS; `0x30000000-0x3FFFFFFF` heap; `0xC0000000-0xCFFFFFFF`
  RSX local; `0xD0000000-0xDFFFFFFF` memória principal mapeada por IO do RSX.

### ADR-ARCH-5 — Resolução de imports por NID com fallback `CELL_ENOSYS`
- fonte: `docs/ARCHITECTURE.md` · status: **proposto**
- Pares `(module, NID)` extraídos dos stubs de import, resolvidos por `tools/nid_db.json`
  e substituídos por chamadas à implementação HLE. NID não resolvido loga aviso e retorna
  `CELL_ENOSYS`.

### ADR-ARCH-6 — Arquitetura RSX em três camadas
- fonte: `docs/ARCHITECTURE.md` · status: **proposto**
- `cellGcmSys` (HLE) → RSX Command Processor (`rsx_commands.c`, FIFO NV47xx) → backend
  gráfico atrás da vtable `rsx_backend`.

### ADR-ARCH-7 — Inventário de backends gráficos conforme o ADR
- fonte: `docs/ARCHITECTURE.md` · status: **proposto**
- Backends: Null (janela Win32 com clear color) e D3D12; Vulkan listado como *Planned*.
- **nota (mantida intacta):** contradito pela intenção de nível PRD e pelo mapa
  verificado do código, que registram um backend **Metal** nativo
  (`libs/video/rsx_metal_backend.m`) existente e **default no Darwin**. Ver
  `INGEST-CONFLICTS.md`, WARNING "Stale ADR backend inventory vs Metal-first reality".
  **Este ADR é conhecidamente desatualizado; não deixar a precedência ADR sobrepor a
  política Metal-first.**

### ADR-ARCH-8 — Ordem de estratégias SPU: HLE, depois interpretador, depois recomp
- fonte: `docs/ARCHITECTURE.md` · status: **proposto**

### ADR-ARCH-9 — RPCS3 como oráculo somente-leitura na prática de implementação
- fonte: `docs/ARCHITECTURE.md` · status: **proposto**

### ADR-PLAT-1 — `#ifdef` inline em vez de camada de abstração
- fonte: `docs/PLATFORM_ABSTRACTION.md` · status: **proposto**

### ADR-PLAT-2 — Mapeamento Win32↔POSIX por subsistema
- fonte: `docs/PLATFORM_ABSTRACTION.md` · status: **proposto**
- Threads, sockets, timers (base de tick PS3 79.800.000), memória virtual, RNG, filesystem.

### ADR-PLAT-3 — `CRITICAL_SECTION` em vez de `CreateMutex` no Windows
- fonte: `docs/PLATFORM_ABSTRACTION.md` · status: **proposto**

### ADR-PLAT-4 — Seleção de backends de áudio e input por plataforma
- fonte: `docs/PLATFORM_ABSTRACTION.md` · status: **proposto**
- Áudio: Windows WASAPI (fallback SDL2); Linux SDL2; macOS SDL2 com CoreAudio como
  "futuro potencial". Input: XInput no Windows, SDL2 GameController em Linux/macOS.
- **nota (mantida intacta):** contradito pela política Apple-first de nível PRD
  (AudioQueue/AudioUnit/CoreAudio preferidos no macOS) e pelo mapa do código, que
  registra CoreAudio/AudioToolbox ligados no Apple com ring buffer em
  `libs/audio/coreaudio_ring.h`, testado por `libs/audio/tests/test_coreaudio_ring.c`.
  Ver `INGEST-CONFLICTS.md`. **Conhecidamente desatualizado.**

### ADR-PLAT-5 — Fibras/corrotinas e a ressalva do macOS
- fonte: `docs/PLATFORM_ABSTRACTION.md` · status: **proposto**
- Win32 fibers no Windows; `ucontext` em POSIX. `ucontext` é deprecado no macOS mas
  funciona; para produção considerar `setjmp`/`longjmp` com stack manual.

### ADR-RPCS3-1 — RPCS3 é spec viva e oráculo de trace, nunca dependência ligada
- fonte: `docs/RPCS3_AS_REFERENCE.md` · status: **proposto**

### ADR-RPCS3-2 — Fronteira GPLv2: ler para entender, reimplementar clean-room
- fonte: `docs/RPCS3_AS_REFERENCE.md` · status: **proposto**

### ADR-RPCS3-3 — A lição M2: medir o lift antes de inventar HLE
- fonte: `docs/RPCS3_AS_REFERENCE.md` · status: **proposto**
- Precedente: o watchpoint "ninguém escreve `0x86E118`" era verdadeiro enquanto a
  conclusão "falta SPURS" era falsa — o lift antigo escrevia em outro EA (`rA=0` mal
  modelado). Regra: uma lacuna de cada vez.

### ADR-RPCS3-4 — Critério de aceite de uma sessão de RE com RPCS3
- fonte: `docs/RPCS3_AS_REFERENCE.md` · status: **proposto**
- Só conta se produzir (1) log/extrato do RPCS3 com ordenação temporal, (2) log do recomp
  no mesmo trecho de boot e (3) tabela de lacunas com **exatamente uma** ação implementável.

### ADR-RPCS3-5 — Antipadrões explícitos ao usar o emulador como referência
- fonte: `docs/RPCS3_AS_REFERENCE.md` · status: **proposto**
- Proibido: forjar estado de FSM (`st620`/EOS) porque o RPCS3 avança; assumir que um HLE
  do RPCS3 mapeia para um patch pontual; copiar arquivos GPL; usar "roda no RPCS3" como
  aceite do port; casar o escalonamento/timing do RPCS3.

</decisions>

---

## Conflitos em aberto (não resolvidos por precedência)

Detalhe completo em `.planning/INGEST-CONFLICTS.md` (0 bloqueadores, 5 avisos, 16 infos).

| # | Conflito | Efeito no planejamento |
|---|---|---|
| 1 | Ciclos de referência cruzada (4, todos de 2 nós, backlinks índice↔filho) | Rebaixados de BLOCKER a WARNING de forma declarada. Sem impacto de escopo. |
| 2 | **Stack Metal M2–M10**: GREEN em 2 PRDs, pendente em 2 PRDs | `REQ-track-g-metal-content-stack` fica **[CONTESTED]**. Qualquer fase que dependa dele declara a dependência como **condicional**. |
| 3 | Inventário de backends do ADR vs realidade Metal/CoreAudio | ADR-ARCH-7 e ADR-PLAT-4 carregam `nota` de obsolescência; precedência ADR **não** sobrepõe a política Metal-first. |
| 4 | API RSX no macOS: Vulkan-first (`MACOS_PORT_PLAN`) vs Metal-first (Metal INDEX, PLAN_READY) | `REQ-macos-port-layers` marcado [SUPERSEDED]; direção Apple-first mantida, divergência declarada. |
| 5 | Aceite de reprodutibilidade: exato-a-partir-do-manifesto vs equivalente-funcional-por-gates | Preservados como dois requisitos distintos (`REQ-reliability-recompiled-quality-gate` e `REQ-rdy5-gow2-reproducible-build`). Escolher um é decisão pendente do autor. |

## Estado verificado do código (âncoras de realidade)

Do mapa em `.planning/codebase/` mais verificação direta nesta sessão:

- **Sem cobertura automatizada do port**: a CI cobre o motor (6 suítes Python + CTest em
  3 SOs, só macOS bloqueante) e **nenhum** lift de título real, **nenhum** dos 73
  `patch_*.py`, **nenhum** smoke de boot. "CI verde" é sinal sobre o motor e sinal
  **nenhum** sobre o port.
- `tools/audit_fallthrough.py` existe e seus testes rodam na CI, mas **nunca** é
  executado contra saída de lift real.
- `games/gow2/verify_lift_baseline.sh` (32 linhas) + `lift_baseline/MANIFEST.tsv`
  (247 linhas) já existem; os snapshots `lift_baseline/injected_00*.cpp` somam **1662
  linhas** de código host que hoje **não são referenciados por nenhum script de build**.
- `games/gow2/apply_all_patches.sh` (319 linhas) tem `--check`, mas **não** tem
  `--status <tsv>` e **não** existe `PATCH_CATALOG.tsv` no repositório.
- `smoke_relift_equiv.sh` **não existe**.
- `tools/ppu_lifter.py` **já emite** `#include <stdlib.h>` (`:146`) e o ramo
  `#elif defined(__APPLE__)` do TLS de `g_trampoline_fn` (`:378-379`); a emissão de
  `ppu_stubs.cpp` continua **ausente**.

## Current Milestone: v1.4 — integrar o que o upstream avançou e nós não temos

**Goal:** trazer, por cherry-pick medido e nunca por merge total, as capacidades que o
`sp00nznet/ps3recomp` desenvolveu em branches vivos e que o nosso fork não tem — fechando
lacunas de correctness e de arquitetura no SPU e no GCM.

**Target features:**
- Interpretador SPU com fallback para alvos indirectos (`bi $reg`), overlays trazidos por
  DMA e código auto-modificado — hoje esses caminhos não têm bytes estáticos para liftar.
- Pacing da publicação do fence GCM, para que um `SET_REFERENCE` intermédio não fique
  invisível a quem espera por igualdade.
- Correcção do `sys_spu_image_import` (offset do EA de origem do segmento).
- Veredicto medido sobre SPURS: continuar o HLE escrito à mão ou liftar o `libsre.prx`
  real (LLE), com o custo de cada via estimado a partir de evidência, não de opinião.

### O que motivou este marco (medido a 2026-08-12)

Comparação do nosso fork contra `upstream/fix/fold-merge-dropped-fixes`: **461 commits
que não temos**. O `upstream/master` está parado em 2026-07-16 e esconde ~230 desses —
comparar só contra o master dava uma leitura desactualizada por semanas.

Quatro lacunas com âncora verificada no nosso código:

1. `runtime/spu/spu_workload.h:6` declara textualmente *"We do not interpret SPU ELFs at
   runtime — we statically lift each"*. Logo `bi $reg`, overlays por DMA e código
   auto-modificado caem no vazio. O upstream tem `runtime/spu/spu_interp.c` +
   `spu_lockstep.c`, partilhando o mesmo `spu_context` e os mesmos helpers.
2. `libs/video/cellGcmSys.c:615` escreve cada `SET_REFERENCE` à medida que drena. O
   upstream mediu o efeito disso: uma thread à espera do fence `0x13` enquanto o
   contador subia `0x51 → 0x1F4`, e uma intro a 2 FPS por ~46 esperas de
   `cellGcmFinish` por frame de filme.
3. `runtime/syscalls/lv2_register.c:825-826` escreve o EA de origem do segmento como um
   u64 big-endian (`+0x10`=0, `+0x14`=addr), mas `sys_spu_segment.src` é um **u32 em
   `+0x10`** — um guest que faça `read32(seg+0x10)` lê zero e faz DMA do endereço 0.
   Bug latente: **não há prova de que o GoW2 o exercite** (`grep image_import games/gow2/`
   → zero).
4. O upstream abandonou o SPURS HLE escrito à mão (o nosso `libs/spurs/spurs_kernel.c`) e
   passou a liftar o `libsre.prx` real, documentado no `docs/FIRMWARE_LLE.md` deles, com o
   argumento de que o HLE "tem sido fonte de problemas sem fim em essencialmente todos os
   títulos SPURS".

### O que NÃO entra por já estar feito ou por estarmos à frente

- **O oráculo RPCS3 já está entregue** (`tools/rpcs3_probe/`, commit `1096c6d`, selftest
  57/57, RPCS3 v0.0.42-19734 + firmware 4.93 instalados). O E81 mediu que, nesta
  configuração, ele **pára antes de nós** na parede da intro (contador `0x592668` em 2
  contra o nosso 3) — logo **não arbitra essa parede**. Serve outras.
- **Estamos à frente no `ppu_lifter`** (5023 vs 3866 linhas): já temos as três correcções
  que o upstream restaurou a 2026-08-09, e a nossa descoberta de jump tables experimenta
  ambos os operandos do `lwzx` e fica com o que valida mais alvos, em vez do "primeiro
  operando carregado do TOC ganha" das duas linhagens deles. Isto é candidato a
  **contribuir de volta**, não a importar.

Detalhe: `docs/re_sessions/2026-08-12-E80-*.md`, `docs/re_sessions/2026-08-12-E81-*.md`.

## Marcos

Marco 1 (v1.0, fechado 2026-07-30) tem granularidade executável (5 fases, arquivadas).
Marco **v1.1** (activo): boot / AUTO_LOAD / walls — ver `Current Milestone` acima e `STATE.md`.

### Marco queued: v1.2 — Toolchain Go (PoC com parity)

**Não activo.** Planeado 2026-08-02. Não altera o foco do v1.1.

| | |
|---|---|
| **Goal** | Provar se partes de `tools/` (ELF → disasm → bounds → lift golden) em **Go** batem parity com Python e valem PROMOTE, sem reescrever o runtime nem o path default de build |
| **Requisitos** | [`.planning/milestones/v1.2-REQUIREMENTS.md`](milestones/v1.2-REQUIREMENTS.md) |
| **Roadmap** | [`.planning/milestones/v1.2-ROADMAP.md`](milestones/v1.2-ROADMAP.md) — fases 12–15 |
| **Aceite** | GO-01…GO-12 + veredicto PROMOTE/HOLD/HYBRID |
| **Fora** | Full re-lift GoW2, apagar Python, patches, Metal, midasm Xenon em Go (Xenon aterra em Python noutro plano) |

Ativar só com `/gsd-new-milestone` (ou workstream) quando o usuário pedir — o `STATE.md` do v1.1 não é roubado por este queue.

Marcos posteriores (motor vs port GoW2) continuam esboçados no `ROADMAP.md`.

## Evolution

This document evolves at phase transitions and milestone boundaries.

**After each phase transition** (via `/gsd-transition`):
1. Requirements invalidated? → Move to Out of Scope with reason
2. Requirements validated? → Move to Validated with phase reference
3. New requirements emerged? → Add to Active
4. Decisions to log? → Add to Key Decisions
5. "What This Is" still accurate? → Update if drifted

**After each milestone** (via `/gsd-complete-milestone`):
1. Full review of all sections
2. Core Value check — still the right priority?
3. Audit Out of Scope — reasons still valid?
4. Update Context with current state

Ver: `.planning/ROADMAP.md`, `.planning/REQUIREMENTS.md`, `.planning/STATE.md`, `.planning/MILESTONES.md`.
