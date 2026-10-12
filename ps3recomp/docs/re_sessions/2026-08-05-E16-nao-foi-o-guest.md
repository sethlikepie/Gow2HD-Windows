# E16 — não foi o guest a escrever: a vigia deu zero, com o controlo a passar

**Data:** 2026-08-05 · **Antecessor:** [E15](2026-08-05-E15-a-string-no-endereco-4.md)

Vigia nova de escritas em memória baixa (`PS3_WATCH_LOWMEM`, motor, gated e OFF
por default, commit `91a9a42`). O resultado **refuta a minha hipótese** e
estreita o alvo.

---

## O resultado

`PS3_WATCH_LOWMEM=0x1000`, receita menu-fast, 90 s:

```
linhas=3925   LOWMEM=0   POOLIDX MAU=1
```

**Zero escritas em `[0, 0x1000)`** — e o caso mau ocorreu na mesma corrida.

## O controlo, que é o que dá valor ao zero

Um zero sem controlo é indistinguível de um instrumento partido. Foi assim que
o E12 quase me enganou. Duas verificações:

| | |
|---|---|
| símbolo no binário | `nm` → 7 ocorrências de `ppu_lowmem_write_hit` |
| a vigia dispara? | com `PS3_WATCH_LOWMEM=0x50000000` → **4 acertos**, simbolizados: `ra=gow2_midasm_Ce03cWaitIdle+0x118` |

O instrumento funciona e simboliza. **O zero é um negativo genuíno, não uma
ausência de medição.**

## O que isto refuta

> **Nada escreve em `[0, 0x1000)` através de `vm_write8/16/32/64`.**

A hipótese do E15 — "alguém escreve a string através de um ponteiro nulo" —
estava certa no efeito e **errada no autor**: não foi código liftado. Todas as
escritas do guest passam por `vm_write*`, e nenhuma tocou lá.

## A string é construída em runtime

O `/r_perma.wad` exacto **não existe no `EBOOT.ELF`**:

| procurado | ocorrências no ELF |
|---|---|
| `/r_perma.wad` | **0** |
| `r_perma` | 1 |
| `rma.` | 1 |

O literal no binário é `r_perma.wad_ps3`, **sem a barra inicial**. A string que
está na memória tem `/` à frente, logo é **montada em runtime** (prefixo de
montagem + nome), num buffer.

Isto não abala a aritmética do E15: o `*(9)` do E12 é uma leitura explícita, e
só dá `rma.` se a string começar no endereço 4. O que muda é a **proveniência** —
não é dado estático mapeado ali, é um buffer preenchido durante a corrida.

## Onde isso deixa o alvo

Se o guest não escreveu e a string é construída em runtime, quem a escreveu foi
**código host** — uma função HLE a fazer `memcpy` para dentro da memória do
guest, através de `vm_ptr8()` em vez de `vm_write*`. Um destino ~0 vindo do
guest (buffer nulo passado a uma chamada de sistema) explica tudo:

```
guest passa um buffer destino nulo a uma HLE de caminhos
HLE host faz memcpy(vm_base + ~0, "/r_perma.wad...", n)   <- NAO passa por vm_write*
  -> a string fica no endereco guest 4
  -> *(4) e *(9) sao depois lidos como this e como head   (E12, E14, E15)
```

Os candidatos naturais são as camadas que manipulam caminhos: `cellFs*`,
`movieio`/FIOS (que no log aparece como `[FIOSOPEN]` com paths completos), e o
resolvedor de WADs.

## Refutado por medição — não voltar a perseguir

1. **O guest escrever a string** (hipótese do E15) — zero escritas em
   `[0, 0x1000)` via `vm_write*`, com controlo a passar.
2. **A string ser dado estático do ELF mapeado no endereço 4** — `/r_perma.wad`
   não existe no ELF; o literal é `r_perma.wad_ps3`, sem barra.
3. Tudo o que E11–E15 já refutaram.

## Próximo (E17)

1. **Vigia do lado HLE.** A mesma ideia, mas onde as HLE escrevem: um watch nos
   pontos em que código host copia para memória do guest com destino em
   `[0, 0x1000)`. O caminho mais barato é instrumentar o `vm_ptr8()` usado como
   destino de `memcpy`/`strcpy` nas camadas de path, não todas as HLE.
2. Em alternativa, e mais barato ainda: `[FIOSOPEN]` já imprime paths — ver se
   alguma chamada recebe um buffer destino nulo.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.

## Nota de método

Duas vezes seguidas nesta sessão um resultado nulo apareceu, e as duas vezes a
pergunta certa foi a mesma: *o instrumento está a funcionar?* No E12 a resposta
foi "não, ficou sem tecto" e o zero era inconclusivo. Aqui foi "sim, dispara e
simboliza" e o zero **vale como refutação**. A diferença entre os dois casos não
se vê no resultado — vê-se só no controlo, e o controlo custou uma corrida de
25 segundos.
