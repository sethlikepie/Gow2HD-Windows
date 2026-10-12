# E81 — o oráculo está MAIS preso do que nós

**Data:** 2026-08-12 · **Antecessor:** [E80](2026-08-12-E80-o-censo-do-slot-0x1488-e-a-tabela-OPD.md)

O E80 desenhou o experimento: ler `+0x0`/`+0x4` de `0x008691A8` no nosso runtime e
comparar com o RPCS3 (`0` / `3`). Corrido. O resultado fecha a via — e por uma
razão diferente da prevista.

---

## 1. O experimento desenhado: os dois lados BATEM

Nosso runtime, `boot_gow2` (5 Ago), 30s, `PS3_WATCH_W32=0x008691A8,0x008691AC`.
Uma única escrita apanhada em toda a corrida:

```
[WATCHW32] [0x008691AC]=0x00000003 ra0=_Z13func_002B2F28P11ppu_context+0x2B4
                                    ra1=ps3_indirect_call+0xB60
```

| Palavra | RPCS3 | Nós |
|---|---|---|
| `*(0x008691A8 + 0)` | `0x00000000` | **nunca escrita** (0 hits) |
| `*(0x008691A8 + 4)` | `0x00000003` | `0x00000003` |
| escritor de `+0x4` | (implícito) | **`func_002B2F28`**, via `ps3_indirect_call` |

**A hipótese do E80 §5 está REFUTADA.** Conjecturei que a callback `0x002B2F28`
talvez nunca fosse invocada do nosso lado. É invocada — e exactamente pelo
caminho que o E80 §3 previu estaticamente: chamada indirecta através da tabela
de OPDs em `0x0052F410`. A previsão estática bateu com a medição, nos dois lados.

**Consequência:** a divergência **não está** neste struct. Via fechada.

## 2. O contador da intro: a comparação que interessava

O laço de espera da intro é `func_000CE0A0`, e a sua condição de saída lê-se
directamente do EBOOT:

```
000CE0CC  lwz   r31, -0x5E70(r2)   ; TOC 0x541178-0x5E70 = 0x0053B308 -> 0x00592668
000CE0F4  lwz   r0, 0x0(r31)       ; <- frame #5 da pilha do RPCS3
000CE0F8  cmpwi cr7, r0, 4
000CE0FC  bne   cr7, 0xCE0D4       ; != 4 -> repete
```

(Bate com o comentário que o nosso `ppu_loader.cpp` já tinha: contador em
`0x592668`, skip-flag em `(*[TOC-0x5E34])+4` com o objecto em `0x006FF480`.)

| Instante | RPCS3 `0x592668` | pc do RPCS3 | Nosso `st620` |
|---|---|---|---|
| 20 s | **2** | `0x002B3CE8` | — |
| 30 s | — | — | **3** |
| 45 s | **2** | `0x002B3CE8` | — |
| 75 s | **2** | `0x002B3CE8` | — |

Três corridas independentes do RPCS3 (uma por medição — ver §3). O contador
**não se move de 2**, e o `pc` fica sempre no mesmo `sc` = `sys_timer_usleep(500)`
do laço em `0x002B3CC8`, à espera de um byte que não muda.

## 3. A conclusão que reorienta o trabalho

**O RPCS3, tal como está configurado, pára ANTES de nós.** Contador 2 contra o
nosso 3, e nenhum dos dois chega ao 4 que abre o laço.

Portanto **o RPCS3 não arbitra esta parede.** Não há aqui um «como o hardware
faz» para copiar: a referência está encalhada no mesmo sítio, um degrau atrás.
Construir estratégia de comparação sobre ele, para esta parede, seria trabalho
perdido — que é precisamente o que este experimento evitou.

### Ressalvas que limitam a força da conclusão

1. **Configuração degradada.** O `EBOOT.BIN` é NPDRM e exige o `.rap` da consola
   do utilizador, que não temos; o RPCS3 está a arrancar o **`EBOOT.ELF`
   decifrado** solto. Uma instalação NPDRM própria pode comportar-se de outra
   maneira.
2. **macOS/arm64 do RPCS3 é experimental** (`v0.0.42-19734`, MoltenVK).
3. **Não sei o tier de compatibilidade do GoW2 HD no RPCS3.** Se for «Intro»,
   este resultado é o esperado e não diz nada sobre hardware real.

Nenhuma delas foi verificada. A conclusão firme é a estreita: **nesta
configuração, o RPCS3 não passa daqui.**

### O que ainda tornaria o oráculo útil

- Obter o `.rap` e instalar o jogo como NPDRM a sério.
- Ou um RPCS3 x86/Windows, onde o suporte é maduro.
- Para **outras** paredes (não esta), o oráculo já funciona: pilha de 12 frames
  validada, leituras de memória, desmontagem — ver E80 §6.

## 4. Terceira limitação do stub

`D` (detach) **mata a thread do GDB Server** — mesmo modo de falha do
`read_char()` do §5a do E80. Depois de um detach o stub não volta a aceitar
ligações nessa corrida do emulador. Logo: **um attach por corrida**; amostrar ao
longo do tempo exige uma corrida do emulador por amostra (foi assim que a tabela
do §2 foi obtida). Registado no docstring de `live_check.py`.

## 5. Aceites

| Item | Estado |
|---|---|
| `+0x4 == 3` nos dois lados, escritor identificado | **medido**, ambos os lados |
| Hipótese «a callback não corre no nosso lado» | **REFUTADA** |
| Contador do RPCS3 parado em 2 (20/45/75s) | **medido**, 3 corridas |
| Nosso contador em 3 aos 30s | **medido**, 1 corrida |
| «O RPCS3 não passa esta parede **em hardware real**» | **NÃO estabelecido** — só nesta configuração |
