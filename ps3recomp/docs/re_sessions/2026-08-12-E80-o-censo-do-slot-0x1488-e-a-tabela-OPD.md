# E80 — o censo do slot `-0x1488(r2)`: ninguém escreve `+0x0` no EBOOT

**Data:** 2026-08-12 · **Antecessor:** [E79](2026-08-08-E79-o-disjuntor-so-dispara-em-alvo-nao-resolvido.md)

Sessão de *tooling*, não de boot: trouxe o oráculo RPCS3 do upstream
(`tools/rpcs3_probe/`) e, ao construir a fixture do seu self-test a partir do
EBOOT real, caíram três factos estáticos sobre a parede.

**O que NÃO é novo:** o laço de poll em `func_002B2E04` / `0x002B2E2C`, a
condição de igualdade (`r0==0` repete), e o slot `-0x1488(r2)` estão
documentados desde 2026-07-23 — ver
`games/gow2/notes/2026-07-23-2b2e04-typewrites-diagnostic.md` e a sonda já
existente `recomp_mid_v2/patch_2b2e04_flag_probe.py`. Nada aqui contradiz isso.

---

## 1. O EA da flag, resolvido estaticamente

A sonda existente diz que o ponteiro é «dinâmico — resolve-se aqui para depois
se poder apontar `PS3_WATCH_W32=<esse endereço>`», isto é, planeava obtê-lo por
**medição em boot**. Ele lê-se do ficheiro:

```
e_entry 0x00519570 -> OPD {code=0x00010230, TOC=0x00541178}
lwz r31,-0x1488(r2)  ->  slot TOC em 0x0053FCF0
*(0x0053FCF0)        =  0x008691A8      <- r31 no load
flag do laço          =  *(0x008691A8) 32-bit BE
```

`0x008691A8` cai no segmento 1 (`vaddr 0x510000`, `filesz` termina `0x576EA4`,
`memsz` termina `0x9B9808`) — logo está **no `.bss`: zero no load**. O laço é
desenhado para girar até alguém escrever.

> **RESSALVA (assunção, não facto):** isto é o valor de *load-time*. Se algo
> reescrever o slot `0x0053FCF0` em runtime, `r31` deixa de ser `0x008691A8`.
> O sítio P1 da sonda existente é que decide. **Não** tratar como confirmado
> antes disso.

## 2. Censo: nenhum escritor de `+0x0` via este slot

Todos os sítios do EBOOT que carregam o ponteiro (`lwz rX,-0x1488(r2)`) — são
**três**, e nenhum escreve o offset que o laço lê:

| Sítio | Reg | Acesso | Detalhe |
|---|---|---|---|
| `0x002B2E1C` | r31 | **lê** `+0x0` | `lwz r0,0x0(r31)` @`0x002B2E20` — o laço |
| `0x002B2F08` | r3 | passa adiante | `addi r3,r3,4; bl 0x002B75C4` → passa `&obj+4` |
| `0x002B2F5C` | r11 | **escreve `+0x4`** | `stw r0,0x4(r11)` @`0x002B2F80`, com `r0=3` |

Consequência: **`0x008691A8` é um struct, não uma flag solta** — `+0x0` é o que
o laço espera, `+0x4` é um campo de estado que recebe 3. E o escritor de `+0x0`
**não alcança o objecto por este slot do TOC** em nenhum ponto do EBOOT; chega
por argumento/`this`. Procurar o escritor por `-0x1488(r2)` é, portanto, um beco
— o que fecha uma via de busca em vez de abrir uma.

## 3. Uma tabela de OPD não registada até agora

`0x002B2F28` — a função que contém o escritor de `+0x4` — aparece como palavra
de dados em `0x0052F418`, dentro de uma tabela de descritores de função:

```
0x0052F410:  002B2EEC 00541178      <- OPD #1
0x0052F418:  002B2F28 00541178      <- OPD #2  (o escritor de +0x4)
0x0052F420:  002B3030 00541178      <- OPD #3
```

Três OPDs contíguos com o mesmo TOC = uma **tabela de callbacks** passada a
alguma registação de sistema. `0x002B2F28` também é chamada directamente de
`0x002B303C` e `0x002B304C`.

O seu prólogo é uma guarda de handler:

```
002B2F28  li    r0, 0
002B2F2C  cmpwi cr7, r3, 1
002B2F30  ori   r0, r0, 0xFFFF
002B2F34  cmpw  cr6, r4, r0
002B2F38  bnelr cr7            ; volta se r3 != 1
002B2F58  bnelr cr6            ; volta se r4 != 0xFFFF
002B2F5C  lwz   r11, -0x1488(r2)   ; só aqui toca no objecto
```

Isto é, o corpo só corre se for invocada com **`r3==1` e `r4==0xFFFF`**.

**Hipótese (não medida):** se o nosso HLE nunca invocar esta callback com esse
par exacto, `+0x4` nunca recebe 3 e o struct nunca avança. Grep em
`games/gow2/` por `52F41` → zero: a tabela nunca foi olhada. **Experimento
discriminador barato:** encontrar a chamada de registação que consome
`0x0052F410` e ver se o nosso lado a honra.

## 4. O tooling que veio com isto

`tools/rpcs3_probe/` — cliente GDB-RSP para o stub do RPCS3 (breakpoint em EA
guest → registos + memória + callstack ELFv1 desenrolada, em JSON por receita).
Portado para macOS (`rpcs3_host.py`) e exercitado por `fake_rsp_server.py` +
`selftest.py` (**46/46 verdes**), cuja fixture é o EBOOT real e a cadeia de stack
`0x002B2660 ← 0x002B2DE0 ← 0x002B2E30`. Receita: `recipes/gow2_wall_2B2660.json`.

## 5. Instalação real do RPCS3 — o que só apareceu ao ligar

RPCS3 **v0.0.42-19734-26e37d8c (macOS aarch64)** instalado em `/Applications`.
Três coisas que o servidor falso escondeu:

### 5a. `nc -z` na porta do stub MATA o stub

`gdb_thread` chama `read_char()` numa conexão aceite e **termina a thread
inteira do GDB Server** se não chegar byte nenhum (`Emu/GDB.cpp:240`, errno=60).
Um connect-and-close — que é exactamente o que `nc -z` faz — deixa o stub morto
para o resto da sessão:

```
RPCS3: GDB Server: SIG: Thread terminated due to fatal error:
Tried to read char, but no data was available
```

Custou-me duas corridas a diagnosticar, porque o sintoma aparece no *cliente
seguinte*. **Nunca sondar a porta:** esperar um intervalo fixo e falar RSP já.
Documentado no docstring de `live_check.py`.

### 5b. Um pendurar infinito no `rsp.py` do upstream

`_wait_ack` tem um laço interno de preenchimento que **não verifica o
deadline**; `_read_raw()` devolve `b""` em timeout de socket, logo
`while not self.buf: ... continue` gira para sempre quando o stub está mudo.
Contra o RPCS3 real o cliente pendurou indefinidamente em vez de levantar erro.
Corrigido. Um stub falso que responde sempre nunca chega a este caminho — é por
isso que sobreviveu ao selftest offline.

### 5c. O `config.yml` desta versão não tem o formato que o upstream documenta

Não é o bloco `GDB Server: / Enabled: / Port:`. É um escalar em `Miscellaneous:`,
e vazio significa desligado:

```yaml
Miscellaneous:
  GDB Server: 127.0.0.1:2345
```

`rpcs3_host.py` passa a aceitar os dois formatos; o selftest cobre seis variantes.

### 5d. Censo de capacidades (`live_check.py`), medido

| Pacote | Resposta | Leitura |
|---|---|---|
| `qSupported` | `PacketSize=1200` | **sem** `qXfer:features:read`, **sem** `QStartNoAckMode` |
| `?` | `S05` | stub vivo, alvo parado |
| `m10000,10` | `7f454c4602020166…` | **leituras de memória FUNCIONAM** (header ELF em EA 0x10000) |
| `g` | vazio | **registos NÃO** — não existe thread PPU |
| `qXfer:features:read` | vazio | não suportado → usa-se o fallback do `rsp.py` |

Consequência para o selftest: o caminho do `target.xml` que ele validava **não é
o que uma corrida real exercita**. Adicionados checks ao layout de fallback (71
registos, `pc` em `64*8`, offsets contíguos) — que é o caminho real.

## 6. Firmware instalado — e a primeira captura real do oráculo

Firmware **4.93** (`release:04.9300`, CEX-ww) instalado a partir do `PS3UPDAT.PUP`
do utilizador via `--installfw`: 1075 ficheiros em `dev_flash`, incluindo
`libsre.sprx` (o kernel SPURS da via LLE do upstream). `SYS: Firmware version: 4.93`.

### 6a. Um segundo bug de correcção: o layout de registos

Com firmware, `g` passou a devolver dados — e a medição matou o fallback do
upstream. O bloco tem **556 bytes**, não 568:

```
32*8 (GPR) + 32*8 (FPR) + pc8 + msr8 + cr4 + lr8 + ctr8 + xer4 + fpscr4 = 556
```

`cr`/`xer`/`fpscr` são de **4 bytes**. O fallback punha 8 em todos, deslocando
todos os offsets a partir de `cr` — logo `lr` e `ctr`, precisamente os registos
que o stack walk e a análise de icall usam, decodificavam para lixo com aspecto
plausível. Sintoma que o denunciou: `pc` lia **0x00000000**; com o layout
corrigido lê **0x002B3CE8**. Além disso os bytes **520..555 vêm marcados `x`**
(indisponíveis): o RPCS3 **não fornece** `msr`/`cr`/`lr`/`ctr`/`xer`/`fpscr`, e
com o layout certo eles são **omitidos** em vez de adivinhados.

### 6b. Duas limitações do stub, medidas

- **Sem interrupt assíncrono.** `cont` + `\x03` nunca produz stop reply, logo a op
  `snapshot` do upstream não serve. Mas o stub entrega o alvo **já parado** no
  attach (`?` → `S05`) — daí a op nova `attach`.
- **O attach congela o alvo.** Qualquer progresso tem de acontecer *antes* de
  ligar; ligar logo captura `pc=0`. Daí `--boot-wait` (default 22s).

### 6c. A captura (reproduzida em 2 corridas independentes)

`tools/rpcs3_probe/captures/gow2_boot_state.json`, aos 25s de boot. Callstack de
**12 frames, todos validados** por `is_link_branch`:

```
#0  0x002B3CE8   sc            <- sys_timer_usleep (r11=141, r3=500)
#1  0x002B3CDC
#2  0x002B4690
#3  0x002BFC00
#4  0x002C0860
#5  0x000CE08C
#6  0x000CE0F4
#7  0x000B71F8
#8  0x002B2EC8
#9  0x0025C894
#10 0x000104DC
#11 0x00010250   <- entry (OPD code de e_entry = 0x00010230)
```

O RPCS3 está num **segundo laço de poll**, distinto do do §2 — espera um **byte**
em `*(*(r31))+4` com `usleep(500)` entre tentativas:

```
002B3CC8  lwz   r9, 0x0(r31)
002B3CCC  lbz   r0, 0x4(r9)      ; BYTE, nao word
002B3CD4  bne   cr7, 0x2B3CFC    ; != 0 -> sai
002B3CD8  bl    0x4B8F18
002B3CE0  li    r3, 500
002B3CE4  li    r11, 141         ; SYS_TIMER_USLEEP
002B3CE8  sc                     ; <- PC
002B3CF8  beq   cr7, 0x2B3CD8    ; ainda 0 -> repete
```

### 6d. Uma hipótese minha, corrigida pela medição

O §5 anterior conjecturava que a callback `0x002B2F28` talvez nunca fosse
invocada no nosso lado, deixando o struct sem avançar. O oráculo lê, no RPCS3:

```
*(0x0053FCF0) = 0x008691A8
*(0x008691A8 + 0) = 0x00000000     <- a "flag" do §2 e' ZERO no RPCS3 tambem
*(0x008691A8 + 4) = 0x00000003     <- o valor que 0x002B2F28 escreve
```

Duas leituras:

1. **A callback CORRE no RPCS3** (`+0x4 == 3`). A hipótese de "nunca invocada"
   fica sem suporte como explicação geral — no mínimo é preciso comparar com o
   nosso valor de `+0x4`.
2. **`+0x0 == 0` também no RPCS3.** Logo "o `+0x0` tem de ficar não-zero" **não
   é**, por si, o ponto de divergência aos 25s: a referência está no mesmo estado.
   O que distingue os dois lados tem de ser medido no nosso boot, não inferido.

**Próximo experimento (desenhado, não corrido):** ler `+0x0` e `+0x4` no nosso
runtime no mesmo instante de boot e comparar com estes dois valores. Se o nosso
`+0x4` não for 3, a divergência é a callback; se for 3, a divergência está abaixo.

## 7. Aceites

| Item | Estado |
|---|---|
| §1–§3 (EA da flag, censo do slot, tabela OPD) | leitura **estática** do EBOOT |
| §4 (cliente do oráculo) | **offline/unit**, 57/57 |
| §5 (stub vivo, memória lida) | **provado in-emulator** |
| §6 (firmware, layout, captura de 12 frames) | **provado in-emulator**, 2 corridas |
| Breakpoint no muro (`0x002B2DE0`) | **NÃO exercitado** |
| Comparação com o NOSSO boot | **NÃO exercitado** |

**Ressalva:** `EBOOT.BIN` (NPDRM) não arranca sem o `.rap`; a captura usa o
`EBOOT.ELF` decifrado. Montar o jogo em `dev_hdd0/game/NPUA80491/` com symlinks
lança `out_of_range` no RPCS3 — o ELF solto é o caminho que funciona.
