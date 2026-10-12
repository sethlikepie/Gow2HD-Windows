# O paliativo TYPE15 REHOME virou a causa do defeito que devia proteger

Data: 2026-08-01. Tudo medido, mesmo binario (`boot_gow2.jtfix`), so' muda a env var.

## De onde vim

Os 13 `ps3_call_opd(opd=0x00000000)` de `func_0039E6B4` -- o unico achado que
sobreviveu a corrida de controlo sem `PS3_GATE_FORCE` (ver a correccao no fim do
documento da cadeia completa).

`func_0039E6B4` faz duas chamadas virtuais. A segunda:

```
r0  = *(self + 0x44)                 // contador
r9  = *(self + 0x24) + r0*12         // ranhura
r0  = *(r9 + 0)                      // tabela
r11 = 0
r3  = *(r0 + idx*4)                  // ENTRADA
if (r3 != 0) r11 = r3 - 4
r3  = r11
r9  = *(r11 + 0)                     // vtable do produto
r10 = *(r9 + 0x28)                   // opd
ps3_call_opd(r10)
```

Com a entrada a 0, o `r11` fica em 0, le'-se a vtable do endereco guest 0 e
despacha-se `*(0x28)`. O defeito nao esta no despacho: **esta na tabela**.

## A sonda que ja' la' estava mostrava so' a parte boa

`[WADLD-FACT]` tem `n++<16` hard-coded. As 16 primeiras passagens sao todas
saudaveis e as 13 mas' acontecem depois do cap.

**Terceira vez hoje** que um cap fixo esconde exactamente a amostra que
interessa (antes: `PS3_WATCH_STORE` a 300 e `PS3_TRACE_ICALL_TO` a 64). A sonda
nova (`patch_39e6b4_null_entry_probe.py`) nasceu sem cap fixo.

## O que ela mediu

```
[E6B4] VAZIA self=0x47D00000 vt=0x00516D70 cnt=0 base=0x47D00400
       slot=0x47D00400 tab=0x47D00418 idx=2 ent=0x00000000        (x13)
```

13 linhas identicas, 13 `OPD-BAD`. Mesmo evento.

## Quem escreveu ali: nos

`PS3_WATCH_STORE` nas quatro moradas envolvidas -- **todas as escritas** vem de
`ps3_type15_note_resolve`, codigo HOST nosso. Nenhuma vem do jogo:

```
w32 [0x47D00044]=0x0         ra0=ps3_type15_note_resolve+0x1B4
w32 [0x47D00400]=0x40100638  ra0=ps3_type15_note_resolve+0x258
w32 [0x47D00418]=0x40100844  ra0=ps3_type15_note_resolve+0x258
w32 [0x47D00420]=0x0         ra0=ps3_type15_note_resolve+0x258   <- a entrada vazia
w32 [0x47D00400]=0x47D00418  ra0=ps3_type15_note_resolve+0x2F4
w32 [0x47D00418]=0x47D00804  ra0=ps3_type15_note_resolve+0x408
```

`0x47D00000` nao e' um objecto do jogo. E' a "zona pinada" que o paliativo
fabrica.

## O que o paliativo faz, e o que contorna

Nove camadas construidas ao longo de varias sessoes de Julho: SNAP da vtable,
REHOME da fabrica para a zona pinada, REHOME da free-list para pin+0x400, fixup
dos ponteiros auto-referentes, REHOME do "shell" para pin+0x800, RETARGET do
`tab[0x54]`, REPAIR da vtable estampada, `ty15_force_pin_freelist`,
`ps3_type15_freelist_replenish`.

**Todas para sobreviver ao mesmo evento.** O comentario no lift nomeia-o:

    *obj becomes ASCII 0x5F436F75 ("_Cou" from R_Perm "_Count…")

Esse era o `F2B-STREAM-PUMP` a escrever 20 MB numa janela FIXA de 1 MiB
(`0x40080000`), 770 KiB para la' do ring real de 256 KiB, por cima da arena
`0x400Cxxxx`/`0x401xxxxx` onde a fabrica vive -- **corrigido hoje de manha** por
`patch_fios_f2b_pump_ring_bounds.py`.

## E o custo proprio do paliativo

```
[TYPE15] SNAP   obj=0x401002F0 vt=0x00516D70
[TYPE15] REHOME old=0x401002F0 pin=0x47D00000 tab[0x54]=0x47D00000 vt=0x00516D70
                +24=0x47D00400 +44=0x00000000 +48=0x00000000 +C8b=-1 +D4=0x00000000
```

O `SNAP` acontece com **vtable viva** -- o objecto estava sao. E a copia e'
tirada com a fabrica ainda **vazia** (`+0xC8 = -1` e' o cursor "sem produto").
A partir dai `tab[0x54]` aponta para o congelado, e tudo o que o jogo popula vai
para o `0x401002F0` original e nunca mais e' lido. Dai `cnt=0`, dai o slot
vazio, dai os 13 despachos nulos.

## A medicao que fechou o caso

Mesmo binario, so' muda `PS3_TYPE15_REHOME`:

| | REHOME=1 | REHOME=0 |
|---|---:|---:|
| tabela vazia | **13** | **0** |
| OPD-BAD | 14 | **1** |
| ICALL-BAD | 12 | 12 |
| FACTORY REPAIR | 0 | 0 |
| R_PermA lido | 20298800 | 20298800 |
| st620 max | 3 | 3 |
| thr_end | 0 | 0 |

E a prova de que o stomp desapareceu mesmo (`PS3_TRACE_E6B4=all` +
`PS3_WATCH_STORE=0x401002F0`, com REHOME=0):

- **3184 despachos, ZERO vazios.** Oito fabricas distintas, todas com vtable
  viva (0x0051xxxx), todas a resolver produtos reais.
- As escritas em `0x401002F0` sao a cadeia normal de construtores C++ do guest
  (`0x511628 -> 0x5116E8 -> 0x511680 -> 0x516D70`), a terminar exactamente na
  vtable que o SNAP capturava. **Nenhum ASCII. Nenhum stomp.**
- `FACTORY REPAIR = 0`: o reparador defensivo nunca precisa de disparar.

Default invertido para DESLIGADO, 3/3 confirmadas, `[TYPE15]` calado.

## O que isto NAO resolve

`thr_end` continua a **0** nos dois bracos. **A parede do AUTO_LOAD nao e' esta,
e nao foi aberta aqui.** O `ICALL-BAD=12` tambem nao mexeu -- e' um defeito
separado, ainda por diagnosticar. O gate oficial continua a nao passar.

## Licao

Duas regras do CLAUDE.md pagaram-se hoje, e a segunda so' porque a primeira
tinha sido cumprida de manha:

1. **"Suspeitar de nos mesmos primeiro."** O `PS3_WATCH_STORE` nomeou o culpado
   em uma corrida. Sem ele eu teria passado a tarde a estudar a tabela de tipos
   do jogo, que estava correcta o tempo todo.
2. **"Remover paliativos nossos quando a evidencia provar que atrapalham."**
   Um paliativo protege contra um bug; quando o bug morre, ele fica -- e o que
   era proteccao passa a dano, silenciosamente. Nada no sistema avisa.

E uma terceira, que ja' devia ter aprendido as duas vezes anteriores de hoje:
**cap fixo em sonda e' um mentiroso por omissao.** Mostra a parte boa da
amostra e cala a que interessa. Toda a sonda nova leva cap por env var.

Ficou por auditar: as outras oito camadas do TYPE15 e o `ps3_factory_repair_vt`
geral existem pelo mesmo motivo e sao candidatas ao mesmo destino -- mas cada
uma precisa da sua propria medicao antes de sair.
