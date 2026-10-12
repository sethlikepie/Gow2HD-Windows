# E73 — o jogo já está no loop principal, e a cadeia envenena o alocador dele

> **PRECISADO em 2026-08-08 pelo [E75](2026-08-08-E75-a-parede-e-func_002B2660-o-tick-que-nao-volta.md):**
> «degraus 24-30 **e 33** nao aparecem» -- o **degrau 33 nao existe**: a 33a entrada
> da cadeia e' uma sonda de CONTROLO (`func_0039E6B4`), que imprimiu **1593 vezes**.
> Era facto sobre o instrumento, nao sobre a execucao.
> E os degraus 24-30 nao sao ramos nao tomados: `func_002B2DD0` chama tres funcoes
> em linha recta, sem ramo, e so' a **primeira** (`func_002B2660`) imprime. E' um
> ponto unico de bloqueio.
> **SOBREVIVE e melhora:** «o jogo ja' esta' no loop principal» -- esta' no loop **e**
> no frame 1 (`[STATE] frame=1 ... code 0x002B2DD0`).

**Data:** 2026-08-08 · **Antecessor:** [E72](2026-08-08-E72-generalizei-de-duas-linhas-em-doze.md)

Pergunta estratégica: *o que destrava mais o avanço do jogo?* A resposta obrigou
a duas correcções — uma minha, outra do mapa do projecto.

---

## Correcção 1 (minha): o B71 **sai**

Carreguei «o B71 encalha no #60» por oito ledgers (E62, E64, E66–E72). Vinha de
`grep -c 'exit func_000B71B8'` = 0 — **com a sonda desligada**. Ela é gated por
`PS3_TRACE_POSTINTRO`, que eu nunca liguei.

Ligada:

```
[POSTINTRO] exit func_000B71B8 #1 flag_obj=0x00543740 +0x64=1
```

**O B71 completa os 60 passos e devolve.** O `#060` era só o último sítio
instrumentado, não uma paragem.

## Correcção 2 (do mapa): o loop principal **corre**

A sonda `[ALCHAIN]` numera os degraus da cadeia do `main`. Com ela ligada:

```
degrau  1..20   uma vez cada
degrau 21  func_002B2E04    <- "a 8a chamada do main, NUNCA alcancada"
degrau 22  func_00242C94    <- O LOOP PRINCIPAL DO JOGO
degrau 23  func_002B2DD0    (lr=0x00242D04, de dentro do loop)
degrau 31  func_0022851C
degrau 32  func_00411A5C    n=107
```

O cabeçalho do `patch_218340_pool_index_probe.py` diz:

> *«`func_002B2E04` (8ª chamada do main) NUNCA É ALCANÇADA → `func_00242C94`, o
> loop principal do jogo, nunca corre»*

**Está desactualizado.** Os dois são alcançados. O jogo entra no loop principal.

(Degraus 24–30 e 33 não aparecem; podem ser ramos não tomados e não paragens —
não medido.)

## E é aqui que a cadeia dos E41–E61 entra

O único degrau repetido é o 32, `func_00411A5C`, **107 vezes**. E ele é:

```c
undefined4 _opd_FUN_00411a5c(int param_1, undefined4 param_2)
{
  if (*(int *)(param_1 + 300) == 0) _opd_FUN_0022831c();
  uVar1 = _opd_FUN_00263554(*(undefined4 *)(param_1 + 0x114));      /* aloca */
  _opd_FUN_00210720(uVar1, param_2, *(undefined4 *)(param_1 + 0x20)); /* inicializa */
  return uVar1;
}
```

- **`func_00263554`** é o pop da free-list — o mesmo que o E25/E26/E42 apanharam
  a devolver **texto** como objecto;
- **`func_00210720`** é o mesmo que o E41 mediu a escrever os campos de
  `0x40780850`.

**O loop principal do jogo aloca 107 objectos através do alocador exacto que a
cadeia dos E41–E61 envenena.**

A cadeia deixa de ser um beco lateral: é o que degrada o alocador do loop que já
está a correr.

## O que isto muda na estratégia

Deixa de ser «fazer o jogo arrancar» — ele arranca. Passa a ser **«limpar o
alocador do loop»**, e há um experimento que decide se essa é mesmo a alavanca:

> Gate que **não escreve** o índice de classe quando o produto é nulo (deixa o
> valor que lá está, em vez de pôr 0). Se os degraus 24–30/33 aparecerem ou o
> `func_00411A5C` passar dos 107, a cadeia **é** o bloqueador. Se nada mudar,
> não é, e a alavanca está noutro sítio.

Um build e uma corrida. É o passo seguinte.

## Refutado por medição

1. **«o B71 encalha no #60»** — sai; a sonda de saída estava desligada quando
   concluí o contrário.
2. **«o loop principal do jogo nunca corre»** (nota do projecto, 2026-08-01) —
   degrau 22 alcançado, e o degrau 23 é chamado de dentro dele.
3. Tudo o que E11–E17 e E19–E72 já refutaram.

## Próximo

1. **o gate do índice de classe** — decide se a cadeia é o bloqueador (1 build,
   1 corrida);
2. se for: repovoar as fábricas por-tag antes do B71 #41;
3. promover o `PS3_GCM_CB` (já validado nos dois regimes);
4. actualizar o cabeçalho do `patch_218340_pool_index_probe.py`, que ainda diz
   que o loop principal não corre;
5. o intermitente do SPU, parado até reproduzir a pedido (E72).
