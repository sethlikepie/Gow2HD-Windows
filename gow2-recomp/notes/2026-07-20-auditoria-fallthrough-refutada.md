# Auditoria do fallthrough cross-fragment — a abordagem por grep NAO serve

Data: 2026-07-20. Resultado negativo, registado para nao ser re-tentado do mesmo modo.

## O que se queria

O `ps3recomp/CLAUDE.md` pede, nos proximos passos:

> 3. **Auditoria sistematica do fallthrough cross-fragment** (padrao do fix 2550C8) no lift inteiro.

Motivo forte: ha agora **duas** ocorrencias confirmadas do padrao.

1. `func_002550C8` caia em `func_002550E8` em vez de `func_00255178` — foi o fix de
   uma linha que venceu o wall [C] (stream do R_PermA parava em 5.619.712 de
   20.169.344 bytes).
2. `func_002B43C0` emite o fallthrough do guest `0x2B4418` como `func_002B43DC`
   quando o alvo correcto e `func_002B441C` (achado 2026-07-20 pelo probe do FIOS
   open). Latente: as flags `0x20` nao tem o bit `0x200` que levaria la.

## O que tentei, e porque falhou

Heuristica 1 — **"alvo com EA menor que o emissor"** (salto para tras).

```
trampolins cross-fragment no total: 1.145.701
para TRAS (alvo < emissor):           858.766   (75%)
```

75% nao e sinal, e ruido. E a hipotese estava errada de raiz: no caso `2550C8`
tanto o alvo errado (`0x2550E8`) como o correcto (`0x255178`) sao **para a
frente**. "Para tras" nunca foi a assinatura do bug.

Heuristica 2 — **filtrar por distancia** (saltos para tras > 64 bytes).

Sobram 851.884. E o topo da lista sao coisas como `func_005078B8 ->
func_00000000` repetido, que e despacho por tabela.

## A razao de fundo

`g_trampoline_fn = (void(*)(void*))func_XXXXXXXX; return;` e a forma que o lifter
usa para **todo** o controlo de fluxo entre fragmentos: fallthroughs, sim, mas
tambem loops, jump tables e saltos computados. No C gerado essas coisas sao
indistinguiveis — a informacao que separa "isto e um fallthrough" de "isto e um
salto explicito" perde-se na emissao.

## Onde a auditoria pertence

No **`ps3recomp/tools/ppu_lifter.py`**, que ainda tem a informacao: sabe quais
emissoes sao fallthrough de fim-de-bloco e qual o sucessor calculado. A verificacao
util e comparar, para cada fallthrough, o alvo emitido contra o endereco que segue
imediatamente o ultimo instruction do bloco — e reportar quando divergem.

Isso e trabalho no lifter, com os limites de funcao a mao, nao um grep sobre 32
chunks de C gerado. Quem pegar nisto: nao repita o grep.
