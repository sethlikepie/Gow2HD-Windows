# E76 — push e pop usam índices **diferentes** da mesma palavra

**Data:** 2026-08-08 · **Antecessor:** [E75](2026-08-08-E75-a-parede-e-func_002B2660-o-tick-que-nao-volta.md)

Análise estática do grafo de fábricas, feita em paralelo com o E74/E75. Traz um
achado ao nível de instrução, confirma o E54, e **entra em conflito com o E53** —
que fica declarado, não resolvido.

---

## O achado: a assimetria de índice

Verificado no ELF, ao nível da instrução (não é artefacto do decompilador):

| | endereço | instrução | índice usado |
|---|---|---|---|
| **push** `vt[0x40]` | `0x0041F78C` | `rlwinm r9,r0,2,14,29` | `(hdr & 0xFFFF) * 4` |
| **pop** `vt[0x44]` | `0x0041F9B0` | `rldicl r0,r0,48,52` + `rlwinm r9,r0,2,18,29` | `((hdr >> 16) & 0xFFF) * 4` |

**Lêem a mesma palavra (`*(filho[2]+4)`), com a mesma condição de poda, e
indexam a tabela de fábricas por campos diferentes.** O push vai pelo half
baixo; o pop pelos bits 16–27.

Se `low16 != (hdr>>16)&0xFFF` para um nó, o push aterra numa fábrica e o pop
noutra — e o saldo de cada uma deixa de fechar.

**O lifter reproduz as máscaras fielmente** (`ppc_rlwinm(...,2,14,29)` e
`ppc_rldicl(...,48,52)`). **Não é bug de lift.**

## Confirma o E54 por outro caminho

O `vt[0x44]` **não faz clamp**: com o cursor já negativo continua a decrementar.
É exactamente o que o [E54](2026-08-07-E54-o-pop-negativo-escreve-retracto-o-mecanismo-do-E43.md)
mediu no lift (`func_0039DAF4` escreve `cursor-1`) e que me obrigou a retractar o
mecanismo do E43. Duas leituras independentes, o mesmo resultado.

## Não existe repovoamento entre os passos 38 e 41

Fecho transitivo do grafo de chamadas directas a partir das duas únicas coisas
que correm entre eles (`FUN_003731a0`, que é um `strcpy`, e `FUN_000e4950`):
**108 + 70 funções, zero sítios de `vt[0x40]`, `vt[0x44]` ou `vt[0x48]`.**

A pilha tem de sair **já povoada** do passo 38. Não há repovoamento previsto — o
que fecha a pergunta que o E61 deixou em aberto.

E dentro do passo 38 há um `vt[0x44](FACT[22])` **incondicional** em
`FUN_00041d5c`, sem push correspondente em boot frio.

## Conflito declarado com o E53

A análise conclui, estaticamente, que o `peek` de `FACT[22]` devolve 0 e que
`FUN_0024f028` cai no ramo nulo, chamando `FUN_0024e198(0 + 0x1C4, ...)`.

O [E53](2026-08-07-E53-sobra-a-chamada-o-escopo-foi-desmontado-antes.md) concluiu
o contrário **por medição**: o `ra2` das três falhas cai no fragmento do sítio
`0x0024F120`, que é o ramo **normal**.

Evidência a favor do E53, e é forte: as vítimas medidas são
`0x40780850`, `0x40780C20`, `0x40781018`, `0x407810E0` — objectos de heap reais.
Se a travessia corresse sobre `0x1C4`, os elementos visitados estariam em memória
baixa.

**Não decido aqui.** Os dois podem ser verdade em momentos diferentes da corrida.
O discriminador é pequeno: uma sonda no `param_1` de `FUN_0024e198`, a contar
quantas vezes vale `0x1C4`.

## E o enquadramento que isto já não tem

Este trabalho descreve o mecanismo da cadeia dos E41–E61 — e o
[E74](2026-08-08-E74-a-cadeia-nao-e-o-bloqueador.md) mediu, entretanto, que essa
cadeia **não é o que bloqueia o jogo**: 6 escritas más em 1441, e suprimi-las não
move o boot. A parede está em `func_002B2660` (E75).

Fica como **mecânica arrumada de um defeito real a 0,4%**, não como o caminho
para destravar o jogo. É a diferença entre explicar e desbloquear.

## Refutado por medição

1. **«há um caminho que repovoa as fábricas entre os passos 38 e 41»** — fecho
   transitivo de 178 funções, zero sítios de push/pop/peek.
2. **«a assimetria push/pop é artefacto do decompilador»** — verificada nas
   instruções, e o lifter reproduz as máscaras.
3. Tudo o que E11–E17 e E19–E75 já refutaram.

## Próximo

1. **o discriminador do conflito**: contar `param_1 == 0x1C4` em `FUN_0024e198`;
2. **contar divergências `low16` vs `(hdr>>16)&0xFFF`** nos nós percorridos — se
   divergirem, cada nó divergente gera um pop órfão. As sondas `[PRUNE]` /
   `[SIBLING]` já existem, mas em `recomp_macos_v6promo`, não na árvore que
   compilo (`recomp_macos_v2`);
3. **a parede real**: qual dos três ramos de `func_002B2660` (E75);
4. `PS3_GCM_CB` por promover; intermitente do SPU parado (E72).
