# E78 — a parede é um ciclo de wrap de ângulo, e as constantes estão sãs

**Data:** 2026-08-08 · **Antecessor:** [E77](2026-08-08-E77-nao-era-o-indice-de-classe-era-o-slot-do-dono.md)

Três agentes em paralelo mais cinco medições. **O bloqueio tem endereço e forma.**

---

## A cadeia completa, medida degrau a degrau

```
[STATE] frame=1 ... code 0x002B2DD0                    loop principal, frame 1
[B71] func_002B2DD0 #001 -> func_002B2660              (#002/#003 nunca)
[B71] func_002B2660 #001 -> ps3_indirect_call ctr=0x004244C0
[LISTWALK] #1..#15                                     travessia dos gestores (TERMINA)
[B71] func_00423AB4 #001 -> r3=0x401002F0 ctr=0x002A98E8
[TICK] entra func_002A6A94 r3=0x00000001
[TICK] entra func_0032988C r3=0x00001F40               8000
[TICK] entra func_0048B1C8 r3=0x4381A480
[TICK] entra func_00457DBC r3=0x4381A520
[TICK] entra func_00457E44 r3=0x4381A520               <- ULTIMA. Nao volta.
```

Cada passo foi medido, não deduzido. A travessia de lista (`func_004244C0`) que
eu suspeitava ser infinita **não é** — faz 15 iterações e sai.

`func_002A98E8` é o **tick de áudio**: `do{}while(true)` sobre 15 canais com
fades, e chamadas com o id `0x534D5044` = `"SMPD"`. O ciclo dele também termina.

## A forma do bloqueio

`FUN_00457e44` é um **normalizador de ângulo**:

```c
if (param_1 < DAT_00541284) {
    do { param_1 = param_1 + DAT_00541280; } while (param_1 < DAT_00541284);
} else {
    for (; DAT_00541280 <= param_1; param_1 = param_1 - DAT_00541280) { }
}
```

Soma ou subtrai o período até cair na gama. **Sem limite de iterações.**

## As constantes estão sãs — lidas do ELF

| símbolo | bits | valor |
|---|---|---|
| `DAT_00541280` (período) | `0x40C90FDB` | **6.28319 = 2π** |
| `DAT_00541284` (mínimo) | `0x00000000` | 0 |
| `DAT_00541FD8` (divisor) | `0x46FFFE00` | 32767 |
| `DAT_00541FDC` | `0x43B40000` | 360 |
| `DAT_00541FE0` | `0x00000000` | 0 |

Nenhuma é zero onde importa. O período é 2π, como se espera de um wrap de
ângulo.

## E com os valores certos o ciclo nem corre

`func_0032988C(8000)` calcula `8000 / 32767 = 0.244`, e chama com
`(0.244, 0.244, DAT_00541FE0=0, DAT_00541FDC=360, ptr)`.

`FUN_00457dbc` reencaminha os argumentos **3 e 4** para `FUN_00457e44`. Logo
`param_1 = 0`:

- `0 < 0` → falso → ramo `else`
- `6.28319 <= 0` → falso → **o `for` não executa nenhuma iteração**
- retorna

**Com os valores correctos isto termina imediatamente.**

## Logo o suspeito é o que chega, não o que está escrito

Se as constantes estão sãs e a aritmética termina, o que corre em runtime tem de
receber **outro** valor. E a forma do ciclo diz qual:

- `NaN` → ambas as comparações falsas → sai; **não explica**;
- valor **enorme mas finito** → `6.28319 <= 1e30` verdadeiro, e subtrair 2π de
  cada vez leva ~1.6·10²⁹ iterações — **indistinguível de infinito**.

**Hipótese principal: um float enorme a chegar ao wrap.** E como toda esta
cadeia (`func_0032988C` → `func_0048B1C8` → `func_00457DBC` → `func_00457E44`)
passa argumentos em **FPRs**, o candidato natural é a passagem de floats no
lift, não o código do jogo.

Isto é hipótese, não medição. O discriminador é pequeno e é o passo seguinte:
imprimir os FPRs à entrada de `func_00457E44`.

## Dois resultados dos agentes que ficam, independentes desta parede

**1. Assimetria do giant lock.** `sys_event`, `sys_semaphore`, `sys_lwcond` e
`cellSpursEventFlagWait` largam o giant lock antes de bloquear — com comentários
a explicar porquê. **`sys_cond_wait`, `sys_mutex_lock`, `sys_lwmutex_lock`,
`sys_ppu_thread_join` e o join de grupo SPU não largam** (grep por
`ppu_giant_lock_*` nesses ficheiros: zero linhas). Qualquer bloqueio nesses
congela o processo inteiro, não só a thread. É um defeito real à espera de
alguém — mas **não é esta parede**, porque a medição mostra o tick preso em
aritmética de float, não num syscall.

**2. A auditoria do `CLAUDE.md` está fechada, e o resultado é negativo.**
168 379 sítios `g_trampoline_fn`, 80 172 com alvo para trás, e depois da peneira
certa (o alvo é a **entrada** de outra função, ou cai no **meio** dela):
**zero** suspeitos reais. Os 9 candidatos vinham todos de `func_004B9D38`, que
não é código — é rodata descodificado como instruções.

E há uma contra-prova estrutural, lida no `tools/ppu_lifter.py:974,3645-3654`: o
alvo do trampolim de fallthrough é sempre `≥ end > start`. **O lifter não
consegue emitir fallthrough para trás.** O item pode sair do `CLAUDE.md` como
*refutado por construção*.

O sucessor honesto: os **3 482 fallthroughs forward intra-função** (onde o bug
`2550C8` realmente vivia — `0x2550C8`, `0x2550E8` e `0x255178` estão todos
dentro de `FUN_00254c40`) e os **1 002 fragmentos sem fronteira Ghidra**. Esses
exigem disassembly instrução-a-instrução, não `functions.json`.

## Refutado por medição

1. **«a travessia de lista de `func_004244C0` é o ciclo infinito»** — 15
   iterações e sai.
2. **«as constantes do wrap estão corrompidas»** — período 2π, divisor 32767,
   lidos do ELF.
3. **«há fallthroughs para trás por auditar no lift»** — zero, e o lifter não os
   consegue emitir.
4. Tudo o que E11–E17 e E19–E77 já refutaram.

## Próximo

1. **imprimir os FPRs à entrada de `func_00457E44`** — decide entre «float
   enorme» e outra coisa, e custa uma corrida;
2. se for float enorme: subir a cadeia até quem o produz, e verificar a
   passagem de FPRs no lift;
3. a assimetria do giant lock em `sys_cond`/`sys_mutex` — defeito real, fila
   separada;
4. os 3 482 fallthroughs forward intra-função, com disassembly.
