# E33 — não destrava mais: as sondas são grátis e o flake não é contenção

**Data:** 2026-08-06 · **Antecessor:** [E32](2026-08-05-E32-a-init-nunca-correu.md)

Pergunta: o jogo destrava melhor agora? **Não.** Está onde o [E9](2026-08-05-E9-promocao-e-os-dois-falsos-positivos.md)
o deixou. Nada desde então foi um fix — foram oito sondas, todas gated e OFF.

Medido, e não assumido.

---

## Duas corridas de 6, mesma máquina livre, mesmo harness

| | `flips` por corrida | `elo_stopped=nenhum` |
|---|---|---|
| **produção, sem sondas** | 2424, 2417, 2440, **2176**, 2424, 2417 | 5 de 6 |
| **com 8 sondas, OFF** | 2643, 2430, 2424, 2641, 2424, 2425 | 6 de 6 |

`st620=11`, `startseq=2`, `nopic=4` nos doze. Load 1,9–2,8 nas duas.

## 1. As sondas são grátis — confirmado, não presumido

~2420 flips nos dois builds; a diferença cabe no ruído. **"Gated OFF = no-op"
deixa de ser uma afirmação de desenho e passa a ser uma medição.** Vale a pena
porque são oito sondas em caminhos quentes (`vm_write*`, `vm_translate`, o pop
da free-list), e o custo podia não ser nulo.

## 2. Os 7017 flips do E9 eram contenção, não progresso

O E9 mediu flips entre 1046 e 7017 (dispersão de 6,7×) com o emulador Android a
~500 % de CPU. Livre, os dois builds assentam em 2417–2643 (dispersão 1,09×).

**Sob contenção o contador de flips *inflaciona*.** Não é métrica de progresso:
mede quanto o loop de present rodou, e um jogo esfomeado noutro sítio roda mais.
Corrige a leitura implícita do E10, que tratava flips como avanço.

O que *é* progresso, e é estável nas doze corridas: `FIOSOPEN` ~241, `r_perma`,
`setflip_after_rperm` 9–10.

## 3. O flake NÃO é contenção — e isto corrige-me

A produção falhou na run4 de hoje: `r_perma=0`, `FIOSOPEN=14`, 2176 flips.
**Assinatura idêntica à run4 do E9** (`FIOSOPEN=14`).

Máquina livre, load 1,9. O adendo do E10 sugeriu que aquela falha era ritmo de
progresso sob carga. **Não é.** É um flake reprodutível na forma, ~1 em 6,
independente da carga, e o `FIOSOPEN=14` diz onde: o I/O quase não arranca.

Logo o 6/6 contra 5/6 entre os dois builds é **o flake a calhar**, não uma
diferença entre eles. Ler aquele 6/6 como melhoria teria sido o erro fácil.

## Estado real

| | |
|---|---|
| paredes 1 e 4 | quebradas e em produção (E9) |
| boot | `st620=11`, `startseq=2`, `nopic=4`, `r_perma` — estável |
| GATE-01 | passa (5–6 de 6, limiar 4) |
| flake | **~1 em 6, não explicado**, assinatura `FIOSOPEN=14` |
| desde o E9 | zero fixes; oito sondas, custo medido nulo |
| raiz da parede actual | localizada (E32): a init nunca corre para o gestor `0x400C6B50` |

## Próximo

1. **O flake com `FIOSOPEN=14`** passa a ter prioridade: é reprodutível, ~1 em 6,
   e agora sabe-se que não é carga. Uma corrida com trace de I/O na altura certa
   diz se o open falha ou nunca é pedido.
2. A raiz do E32 — quem cria `0x400C6B50` sem passar pela init.
3. (A) do E30 — o path escrito no endereço 0 pelo lado host.
