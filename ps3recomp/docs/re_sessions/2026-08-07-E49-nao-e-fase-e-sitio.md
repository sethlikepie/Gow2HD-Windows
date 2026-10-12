# E49 — não é fase, é sítio: três chamadores acertam sempre, quatro falham sempre

> **ATRIBUICAO REFUTADA em 2026-08-07 pelo [E50](2026-08-07-E50-o-lr-nao-e-o-chamador.md).**
> O `lr` **nao** identifica quem chamou `vt[0x48]`: o lifter so' escreve `ctx->lr`
> em chamadas DIRECTAS, e `ps3_indirect_call` nao o escreve (467 linhas varridas,
> zero escritas). Os oito valores sao retornos de directas para funcoes sem
> relacao (`0x0024E424` -> `func_0020EE8C`, `0x0027C724` -> `func_0024C1F8`,
> `0x002B2DE0` -> `func_002B2660`).
> **SOBREVIVE:** a particao -- 8 contextos dinamicos disjuntos, 253 `ok` contra
> 6 `VAZIO`, nenhum grupo misto -- e com ela a refutacao de «e' fase da corrida».
> Salva-se tambem `FUN_0024e198`, mas por OUTRA via: a decompilacao mostra
> `FUN_0020ee8c` chamada na mesma iteracao, poucas instrucoes antes do `vt[0x48]`.
> **CAI:** a coluna «funcao» lida como chamador, para os outros sete valores (sao
> um cone, nao um ponto), e a vizinhanca `FUN_002b2dd0`/`func_002B2E04`, que
> assentava em ler `0x002B2DE0` como chamador.
> Mantido como registo -- nao construir sobre a atribuicao.

**Data:** 2026-08-07 · **Antecessor:** [E48](2026-08-07-E48-o-salto-e-um-ciclo-e-o-jogo-mesmo-nao-verifica.md)

Sem corrida nova: a sonda `EMPTYFAB` já imprimia `lr` nas **259** chamadas, não
só nas 6 que falham. A análise estava por fazer.

---

## A partição

| `lr` | chamadas | resultado | função |
|---|---|---|---|
| `0x0027C724` | **238** | `ok` | `FUN_0027c6f0` |
| `0x002553D8` | 10 | `ok` | `FUN_00255368` |
| `0x0042CA64` | 5 | `ok` | `FUN_0042ca14` |
| `0x0024E424` | 2 | **VAZIO** | `FUN_0024e198` |
| `0x0024E25C` | 1 | **VAZIO** | `FUN_0024e198` |
| `0x002B2DE0` | 1 | **VAZIO** | `FUN_002b2dd0` |
| `0x002A9A84` | 1 | **VAZIO** | `FUN_002a98e8` |
| `0x00237D38` | 1 | **VAZIO** | `FUN_00237cdc` |

`238 + 10 + 5 = 253` — exactamente as `ok`. `2+1+1+1+1 = 6` — exactamente as
`VAZIO`.

**Nenhum sítio é misto.** Três chamadores acertam em 253 de 253. Quatro
chamadores falham em 6 de 6. Todos os `lr` caem dentro das respectivas funções
(nada de resolução por vizinhança).

## Correcção ao E46

O E46 escreveu que o pedido chega *«um push cedo demais»*, a partir da
adjacência medida `12787 VAZIO / 12788 push`. **A adjacência é real e
sobrevive; a leitura de quase-acerto não.** Num binário determinista, o sítio
`0x0024E424` é consultado duas vezes e falha as duas; `0x0027C724` é consultado
238 vezes e acerta as 238. Isso não é uma margem apertada — é uma diferença de
regime entre chamadores.

Fica marcado no E46.

## O que a partição diz

O contexto que a fábrica devolve é empurrado por `func_0039DA78` e retirado por
`func_0039DAB0` — 176 de cada, num só fio (E47). Os três chamadores que acertam
correm **dentro** de um desses 176 escopos; os quatro que falham correm **fora
de todos eles**.

A pergunta deixa de ser *«porque é que a fábrica está vazia àquela hora»* e
passa a ser:

> **O que estabelece o escopo, e porque é que estes quatro chamadores correm
> fora dele?**

Duas leituras, e desta vez a diferença importa para o fix:

1. **falta o push** — o jogo devia ter empurrado antes de chamar estes quatro, e
   por alguma razão nossa (HLE, ordem de init, um caminho que não corre) não
   empurrou;
2. **sobra a chamada** — estes quatro chamadores não deviam correr aqui de todo,
   e correm porque algo os alcança fora de sítio.

## Uma vizinhança que vale a pena seguir

`FUN_002b2dd0` ocupa `0x002B2DD0..0x002B2E04`, ou seja **acaba exactamente onde
começa `func_002B2E04`** — endereço já anotado neste projecto (docstring do
`patch_218340_pool_index_probe.py`) como *«a 8ª chamada do main, NUNCA
alcançada»*, aquela cuja falta impede `func_00242C94`, o loop principal do jogo,
de correr.

Um dos seis pedidos falhados sai de dentro da função imediatamente anterior a
esse endereço. Pode ser coincidência de layout — não é medição de relação — mas
é barato de verificar e liga esta cadeia à parede mais antiga do projecto.

## Refutado por medição

1. **«o pedido falha por uma margem de um push»** — o sítio que falha nunca
   acerta, e o que acerta nunca falha, numa corrida determinista (correcção ao
   E46).
2. **«é uma questão de fase da corrida»** — é uma questão de chamador: a
   partição é limpa, 253/253 contra 6/6.
3. Tudo o que E11–E17 e E19–E48 já refutaram.

## Próximo

1. **quem chama `func_0039DA78`** — sonda com `lr` no push, para nomear o que
   estabelece o escopo. É o passo que separa «falta o push» de «sobra a
   chamada»;
2. o mesmo `lr` no pop, para ver onde os 176 escopos abrem e fecham em relação
   aos quatro chamadores falhados;
3. verificar a vizinhança `FUN_002b2dd0` / `func_002B2E04`;
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
