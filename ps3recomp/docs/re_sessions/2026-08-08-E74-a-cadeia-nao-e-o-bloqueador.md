# E74 — a cadeia **não** é o bloqueador: 6 escritas más em 1441, e saltá-las não muda nada

> **SEMANTICA CORRIGIDA em 2026-08-08 pelo [E77](2026-08-08-E77-nao-era-o-indice-de-classe-era-o-slot-do-dono.md):**
> o campo `+6` **nao e' o indice de classe** (esse esta' em `+2`) -- e' o **slot do
> dono** no array de instancias do pool. E o default do jogo e' **nao escrever**:
> com `param_3 == 0` o campo fica com o valor herdado do descritor. Logo o gate
> `PS3_SKIP_NULLTYPE`, que aqui declarei «nao fiel ao CELL», e' na verdade **mais
> fiel** que o comportamento actual.
> **SOBREVIVE, e e' o que importa:** o veredicto. Com o gate ligado, degrau maximo
> 32, loop a 107 voltas e `st620`=11 ficam **inalterados** -- mais fiel e na mesma
> nao desbloqueia. E os numeros 1441/6 mantem-se.

**Data:** 2026-08-08 · **Antecessor:** [E73](2026-08-08-E73-o-jogo-ja-esta-no-loop-principal.md)

O experimento que o E73 propôs. **Correu, e o resultado é negativo** — o que
poupa semanas de investimento na direcção errada.

---

## Primeiro, uma correcção de cobertura

A sonda `VICTIM` do E59 cobria **um** sítio (`func_0024E3D0`). O padrão

```c
ctx->gpr[0] = vm_read32(ctx->gpr[3] + 0x20);
vm_write16(ctx->gpr[N] + 0x6, ctx->gpr[0]);
```

aparece em **sete**:

| função | destino | | função | destino |
|---|---|---|---|---|
| `func_0024E3D0` | `gpr[9]+6` | | `func_0041C558` | `gpr[4]+6` |
| `func_0024E430` | `gpr[21]+6` | | `func_0041E8A4` | `gpr[4]+6` |
| `func_004117B0` | `gpr[4]+6` | | `func_0042C644` | `gpr[4]+6` |
| | | | `func_0042EC60` | `gpr[4]+6` |

Instrumentados os sete (`patch_clsidx_probe_gate.py`):

> **1441 escritas do índice de classe. Seis com produto nulo.**

O E59 disse *«o sítio nunca acerta — 4 escritas, 4 com produto nulo»*. Verdade
para aquele sítio; **falso como retrato do sistema**. A corrupção é **0,4%** das
escritas, não a regra. (E o E41 já tinha medido `func_004117B0` a escrever **2** —
um valor bom — no mesmo campo.)

## O experimento

`PS3_SKIP_NULLTYPE=1`: não escreve quando o produto é nulo, deixa o índice que lá
estava. Mesmo binário, 50s:

| | gate OFF | gate ON |
|---|---|---|
| escritas `CLSIDX` | 1441 | 1441 |
| com produto nulo | 6 | 6 |
| **degrau máximo** | **32** | **32** |
| **`degrau 32` (o loop)** | **107** | **107** |
| `st620` | 11 | 11 |
| `UNCOMMITTED` | 19 | **40** |

## O veredicto

**A cadeia dos E41–E61 não é o que bloqueia o avanço do jogo.**

Suprimir as seis escritas más não move o boot um passo: o degrau máximo continua
32, o loop principal continua a dar exactamente 107 voltas, a FSM da intro fica
igual. Se a cadeia fosse o bloqueador, alguma dessas três coisas tinha de mudar.

E **piora** a memória não commitada (19 → 40): deixar o índice velho é pior do
que escrever 0. O índice obsoleto aponta para outra classe, e essa produz mais
acessos maus do que a classe 0.

## O que isto salva

Eu estava a um passo de investir em «repovoar as fábricas por-tag antes do
B71 #41» — o fix natural da cadeia. **Teria sido trabalho correcto e inútil.**

Custou um patch e duas corridas descobri-lo.

## O que sobrevive da cadeia

Tudo o que foi medido: as fábricas vazias, os seis produtos nulos, as vítimas, a
ponte do `0x40780850` entre as duas pontas (E59), o `func_00263554` a devolver
texto. É corrupção real e continua a valer a pena arrumá-la um dia.

**Cai** apenas o seu estatuto: não é a parede. É um defeito a 0,4%, num sistema
que já passou por cima dele.

## Refutado por medição

1. **«a cadeia dos E41–E61 é o que impede o jogo de avançar»** — saltar as
   escritas más deixa `degrau máximo=32`, `degrau 32=107` e `st620=11`
   inalterados.
2. **«o sítio nunca acerta»** (E59) — 1441 escritas, 6 más; era 1 sítio de 7.
3. **«deixar o índice velho é mais seguro que escrever 0»** — `UNCOMMITTED`
   passa de 19 a 40.
4. Tudo o que E11–E17 e E19–E73 já refutaram.

## Onde procurar agora

O jogo está no loop principal e dá **107 voltas** ao degrau 32
(`func_00411A5C`). A pergunta passa a ser **porque é que o loop pára às 107**, e
não o que corrompe 0,4% dos índices de classe.

Três análises estáticas a correr em paralelo respondem à parte de que preciso:
quem devia encher as fábricas, o contrato do campo `+6`, e o que são os degraus
24–30 e 33 que nunca aparecem.
