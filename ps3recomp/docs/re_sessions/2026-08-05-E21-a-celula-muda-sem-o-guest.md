# E21 — a célula muda, e não é o guest a escrevê-la

> **PERGUNTA INVALIDADA em 2026-08-05 pelo [E22](2026-08-05-E22-a-sonda-que-recalculava.md).**
> As medicoes deste ledger sao validas, mas incidem sobre `0x00868D54`, celula que o
> E22 mostrou **nao participar** na cadeia. O fix da macro `PS3_WATCH_EA` e a licao
> sobre aplicadores que reportam sucesso mantem-se.


**Data:** 2026-08-05 · **Antecessor:** [E20](2026-08-05-E20-a-celula-que-muda.md)

Vigia de endereço exacto (`PS3_WATCH_EA`, acrescentada ao motor nesta rodada).
Resultado: **zero escritas** em `0x00868D54`, com o instrumento provado, e `B`
a continuar a mostrar dois valores na mesma corrida.

---

## Um erro meu, apanhado pelo controlo

A primeira corrida deu zero. Antes de a interpretar, corri o controlo: vigiar
`0x00869EEC`, endereço que o E16 mediu a ser escrito por
`gow2_midasm_Ce03cWaitIdle`. **O controlo deu zero também** — logo o
instrumento estava partido, não o alvo.

Causa: o meu script de edição fez duas substituições no header, a primeira
pegou (a função `ppu_watch_ea()`) e a **segunda não** (a macro que a chama).
O script imprimiu `PS3_WATCH_EA acrescentada` na mesma, porque só verifiquei
que tinha corrido, não que tinha mudado o que era preciso.

O sinal barato que o confirmou: `strings boot_gow2_e21 | grep PS3_WATCH_EA` →
**0 ocorrências**, com as outras duas envs presentes. A função existia no
código e nunca era chamada, por isso o compilador nem guardou a string.

> É o mesmo defeito que ando a documentar desde o E9, agora cometido por mim
> numa ferramenta minha: **um aplicador que reporta sucesso está a fazer uma
> afirmação, não a dar um resultado.** A verificação barata existe e é uma
> linha — procurar no binário a cadeia que só lá está se o código entrou.

Depois de corrigir com uma edição que falha alto quando não casa: controlo
**1 acerto**, `ra=gow2_midasm_Ce03cWaitIdle+0x118`. Instrumento provado.

## A medição, agora válida

| | |
|---|---|
| escritas em `0x00868D54` | **0** |
| `TOCCHAIN` | 13 chamadas |
| `B` | `0x406387E0` (12×), `0x400C6B50` (1×) |
| `POOLIDX MAU` | 1 |

E o código entre a âncora e o uso é **linha recta** — nenhum `goto`, `if`,
etiqueta ou trampolim entre `492839` e `492884`. Não há caminho alternativo que
explique dois valores.

## O que isto obriga a concluir

Se `A` é o mesmo, o código não ramifica, e ninguém escreve `0x00868D54` via
`vm_write*` — então a célula **é escrita por código que não passa por
`vm_write*`**.

É exactamente o que o E16 já tinha estabelecido para a memória baixa: o caminho
das HLE escreve na memória do guest com `memcpy` sobre `vm_ptr8()`, e essa rota
não é vigiada pela vigia de escritas.

**Isto não está provado** — é a única explicação que sobrevive às medições, não
uma observação directa. Fica como hipótese principal do E22, com o teste já
desenhado.

## Retracto parcial do E20

O E20 escreveu: *"a célula em `0x00868D54` é escrita entre chamadas"*. A
afirmação sobrevive; o que **não** sobrevive é a suposição implícita de que
seria o guest a escrevê-la. Corrigido aqui.

## Refutado por medição — não voltar a perseguir

1. **O guest escrever `0x00868D54`** — zero escritas via `vm_write*`, com
   controlo a passar no mesmo binário.
2. **Haver um caminho de código alternativo em `func_002182A4`** — linha recta
   entre `492839` e `492884`, sem ramos.
3. Tudo o que E11–E14 e E16–E20 já refutaram.

## Próximo (E22)

1. **Estender a vigia de tradução (`PS3_WATCH_LOWPTR`) a um endereço exacto.**
   Ela cobre `vm_translate`, por onde o `memcpy` das HLE tem de passar. É a
   mesma alteração de uma linha que fiz para as escritas, aplicada à outra
   vigia — e apanha o escritor host.
2. Ao fazê-lo, **corrigir também o `ra`**: a `-O0` o
   `__builtin_return_address(0)` aponta sempre para `vm_ptr8` (defeito
   declarado no E17 e ainda por corrigir). Sem isso, a vigia dirá que alguém
   escreveu mas não quem.
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.

## Nota de método

Três vezes nesta sessão apareceu um zero: E12 (sem tecto, **inconclusivo**),
E16 (instrumento bom, **refutação válida**) e agora E21 (instrumento partido,
**inconclusivo** até ser corrigido). O resultado é indistinguível nos três
casos; só o controlo os separa. **O controlo deixou de ser opcional nesta
investigação — passou a ser o primeiro passo, não o último.**
