# Viabilidade: portar um jogo PS3 comercial (estudo de caso: God of War II HD)

> Documento de viabilidade técnica, baseado em **auditoria do código real** do
> ps3recomp (não em aspiração). Última revisão: 2026-06-20.
> Veredito de uma frase: **possível em tese, impraticável hoje** — a fundação PPU
> está em paridade real com o XenonRecomp, mas os dois subsistemas que definem
> "jogável" no PS3 (SPU/SPURS executando + RSX renderizando) estão em 0–30%.

---

## 1. A pergunta

> "Com o estado atual do nosso recompilador, dá para recompilar o GoW2 em um jogo
> jogável, do jeito que outros projetos (N64Recomp, XenonRecomp/UnleashedRecomp)
> já fazem?"

**Resposta direta: não no estado atual — e não "do mesmo jeito", porque o PS3 é
uma classe de problema mais difícil que o N64 e o Xbox 360.** O que existe é real
e relevante, mas "rodar código" não é "jogável".

---

## 2. Estado atual por subsistema (auditado)

| Subsistema | Maturidade | O que funciona (evidência) | O que falta |
|---|---|---|---|
| **Recompilador PPU** (`tools/ppu_lifter.py`, `ppu_disasm.py`) | **Funcional** | int 64-bit, FP escalar, VMX/AltiVec float+int, branches, atomics (`lwarx/stwcx`), `dcbz`. GoW2 → 56.092 funcs, compila e linka (.exe de 45 MB) | `lmw/stmw` (>1300 ocorrências reais no GoW2), `divd/divdu` dot-form, alguns op31 esotéricos, `lswi/stswi`, `mtfsf` real |
| **Loader + Boot** (`runtime/ppu/ppu_loader.cpp`, `ppu_imports.cpp`, `tests/boot_main.cpp`) | **Funcional** | Carrega EBOOT, patcheia imports `.lib.stub` (151 no GoW2), resolve OPD, sobe threads PPU, **executa código real do jogo** | Re-entrada de callback é ponte manual; sem interrupts |
| **Sincronização lv2** (`runtime/syscalls/*`) | **Funcional** | mutex/cond/sema/rwlock/**event-queue**/timer/flags mapeados ao host. `sys_event_init` registra 128–138 corretamente; `lv2_dispatch_syscall` roteia o `sc` liftado | — |
| **HLE de CRT/IO/threads** (`runtime/ppu/ppu_hle.cpp`, `ppu_sysprx.cpp`, `libs/`) | **Funcional** | 975 funções roteadas; cellFs com I/O real; cellSaveData com marshalling **BE-correto**; cellAudio com mixagem real (WASAPI/SDL2); 9 shims BE-aware | ~237 stubs (24%); codecs A/V (cellAdec/cellVdec) são stubs silenciosos |
| **SPU / interpretador** (`runtime/spu/*`) | **Parcial** | Interpretador SPU completo e **testado** (6/6 testes verdes: ISA, DMA/MFC, ELF32→LS, **park/resume cross-thread**, stop-N). Mailboxes/canais/DMA funcionam | **Kernel SPURS não executa** → workloads SPU nunca rodam → ver §4. Lifter SPU existe mas está desconectado do runtime |
| **RSX / GPU** (`libs/hardware/`, `rsx_*`) | **Parcial** | Backend D3D12 de qualidade (device/swapchain/PSO/DrawInstanced/janela); parser FIFO NV47xx; cellGcmSys com state-tracking maduro | **Tradutor de shaders NV40→HLSL: 0%**. Upload/deswizzle de texturas: 0%. `draw_indexed`: vazio. Sem frames de jogo reais |

**Síntese:** o eixo **PPU** (lifter + loader + lv2 + HLE de fundação) é
genuinamente maduro — comparável ao nível PPU do XenonRecomp. Os eixos **SPU** e
**RSX** têm esqueleto avançado, mas a "parte difícil" de cada um (kernel SPURS;
tradutor de shaders) ainda não existe.

---

## 3. Por que PS3 ≠ N64 e ≠ Xbox 360

A diferença não é de esforço — é **arquitetural**.

- **N64Recomp** teve o alvo mais simples: MIPS R4300i é um core escalar sem SIMD
  agressivo nem coprocessadores que travem a recompilação; RDP/RSP são tratáveis.
- **XenonRecomp / UnleashedRecomp** pegaram um alvo **muito favorável**:
  1. Xbox 360 = **3 cores PowerPC simétricos** rodando o **mesmo ISA** do binário
     principal — não há um segundo ISA heterogêneo para emular.
  2. **Não há SPUs** — toda a lógica (física, IA, áudio, geração de comandos de
     GPU) está no mesmo PPC que o recompilador já traduz.
  3. A GPU Xenos é próxima de D3D e a memória é unificada.
  - Mesmo assim, a UnleashedRecomp gastou **muitos meses** reimplementando o
    backend gráfico inteiro (D3D12/Vulkan + tradutor de shaders Xenon) e o HLE
    específico — para **um** jogo.

- **PS3 (Cell)** acrescenta, por cima de tudo isso, **dois subsistemas que o 360
  não tem**:
  1. **6 SPUs heterogêneos** rodando código próprio via SPURS/jobs (física,
     áudio, geração das listas de comando do RSX) → exige emular/recompilar um
     **segundo ISA** e seu **scheduler de kernel**.
  2. **RSX** (GPU Nvidia-like via FIFO/GCM) com swizzle, micro-formatos e shaders
     **NV40** próprios.

> **Fato verificado:** nenhum projeto open-source de recompilação **estática**
> jamais levou um jogo PS3 comercial a jogável. O estado-da-arte para *jogar* PS3
> continua sendo o **RPCS3** — um emulador **dinâmico** (recompila SPU em runtime,
> não estaticamente). É exatamente nos dois eixos que o 360 não possui (SPU + RSX)
> que mora a dificuldade que ninguém venceu estaticamente.

---

## 4. O blocker de boot do GoW2

O EBOOT do GoW2 **carrega, resolve imports, sobe threads PPU e executa código
real** até o ponto de sincronização. O travamento **não é o boot em si** — é um
`sys_event_queue_receive` esperando um evento que nunca chega:

1. O jogo cria uma event queue (`sys_event_queue_create`, 128).
2. Cria um SPU thread group e conecta à queue.
3. Threads PPU chamam `sys_event_queue_receive` (130) e **bloqueiam**.
4. Em um PS3 real, o **kernel SPURS** roda nos SPUs, executa o workload e
   sinaliza a conclusão → o evento é empurrado → o receive acorda.

No nosso runtime, o **kernel SPURS nunca executa** (a HLE `cellSpurs.c` faz
tracking de workloads, mas não roda o policy_module / scheduler). Logo nenhum
evento é empurrado e o receive fica em wait infinito.

> Isto **não é corrigível por patch pontual** — foi provado de 5 formas
> independentes na investigação anterior (watchpoint de hardware confirmou que o
> objeto esperado nunca é escrito; pular o wait leva a loop de ponteiro-lixo;
> sintetizar o objeto falso causa crash). É o sintoma de um **subsistema inteiro
> ausente**. E, mesmo destravando o SPU, o próximo muro é o RSX (sem tradução de
> shaders, não há frames).

O plano concreto e ordenado para atacar esse blocker está em
[`docs/SPURS_BRINGUP.md`](docs/SPURS_BRINGUP.md).

---

## 5. Roadmap até "jogável"

| # | Marco | Esforço | Risco |
|---|---|---|---|
| 1 | Fechar gaps do PPU lifter (`lmw/stmw`, `divd`, op31 esotéricos, `mtfsf`, `lswi/stswi`) | 1–2 p-mês | Baixo |
| 2 | **SPURS real**: materializar imagens SPU no LS, executar o kernel/scheduler SPURS, dispatch de tasksets/jobs, sincronização live PPU↔SPU. *Destrava o boot.* | 6–12 p-mês | **Alto** |
| 3 | **Tradutor de shaders NV40→HLSL/SPIR-V** (vertex + fragment) — a "parte difícil" gráfica | 6–12 p-mês | **Alto** |
| 4 | RSX completo: upload+deswizzle de texturas, SRV/sampler/cache, `draw_indexed`, quads/fans, PSO dinâmico, framebuffer resolve, swapchain | 4–8 p-mês | Médio-alto |
| 5 | Codecs A/V reais (cellAdec/cellVdec via FFmpeg) + HLE específico do GoW2 | 3–6 p-mês | Médio |
| 6 | Bring-up end-to-end (menu→fase, caça a dessincronizações PPU/SPU e correção visual) | 3–6+ p-mês | Alto, cauda longa |

**Esforço total realista: ~24–50 pessoa-meses**, concentrados em SPU e RSX (os dois
maiores blocos). Para uma equipe pequena e muito sênior em emulação/recomp:
**~1,5 a 3 anos de calendário** — com risco real de a interpretação SPU não atingir
tempo real, forçando ligar também o recompilador SPU (o `spu_lifter.py` existe, mas
está desintegrado do runtime).

---

## 6. Conclusão honesta

O que foi construído é **real e impressionante**: recompilador PPU + loader/boot +
sincronização lv2 + HLE de CRT/threads/IO em **paridade genuína** com o nível PPU do
XenonRecomp, e o EBOOT do GoW2 de fato **compila, linka, carrega e executa código
real** do jogo até o ponto de sincronização. A fundação SPU (interpretador, DMA,
park/resume) está **testada e verde**.

Mas "rodar código" ≠ "jogável". Faltam justamente os **dois subsistemas que tornam
o PS3 mais difícil que o 360** — SPU/SPURS executando de verdade e RSX→GPU moderna
com tradução de shaders — e são as partes não-resolvidas do estado-da-arte.

**Recomendação:** tratar isto como **projeto de P&D de fôlego**, não como "falta
pouco". Atacar **SPURS real primeiro** (destrava o boot — ver `SPURS_BRINGUP.md`) e
o **tradutor de shaders NV40 em paralelo**. Cada um é, sozinho, um esforço do
tamanho do que a UnleashedRecomp inteira foi.

**Para *jogar* GoW2 hoje, o caminho é o RPCS3.** Para o **projeto de recompilação**,
existe uma fundação PPU de nível profissional e os dois grandes muros já estão
identificados e dimensionados — que é exatamente o que um projeto sério precisa
saber antes de investir os próximos anos.
