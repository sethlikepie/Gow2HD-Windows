# E61 — o guard do jogo vigia a coisa errada

> **DECODIFICACAO REFINADA em 2026-08-08 pelo [E77](2026-08-08-E77-nao-era-o-indice-de-classe-era-o-slot-do-dono.md):**
> a aritmetica que decodifiquei como «o contexto nao mudou» tem nome mais preciso:
> `param_3 = (WAD do pool == WAD corrente)`. `TABLE[22]` e' o gestor de WADs
> (strings `WAD_R_Perm`, `WAD_R_Hero`) e `TABLE[1]` o de entidades (`EntityCount`).
> E o campo `+6` que a travessia escreve nao e' o indice de classe (esse esta' em
> `+2`) -- e' o slot do dono.
> **SOBREVIVE:** a leitura de que o guard vigia um gestor diferente do que a
> travessia precisa. **CAI:** so' o nome vago «contexto».

**Data:** 2026-08-07 · **Antecessor:** [E60](2026-08-07-E60-o-fecho-e-legitimo-a-travessia-e-que-corre-fora.md)

Última pergunta da cadeia: *porque é que o passo #41 do B71 corre fora de
escopo?* Respondida, e a resposta muda o sítio do defeito.

---

## O passo #38 é quase a corrida inteira

Do trace do B71, com números de linha:

```
 4009  [B71] func_000B71B8 #038 -> func_00041D5C
 6110  [SCOPE] PUSH fab=0x400C6210 cursor=0   (abre o escopo externo)
   ...  ~14 650 linhas: 176 push/pop aninhados
20760  [SCOPE] POP  fab=0x400C6210 cursor=-1  ra3=func_0042A0C8  (fecha)
20767  [B71] func_000B71B8 #039 -> func_003731A0
20768  [B71] func_000B71B8 #040 -> func_000E4950
20769  [B71] func_000B71B8 #041 -> func_0010F5E8
20778  [VICTIM] vitima=0x40780850 PRODUTO-NULO
20804  [B71] func_000B71B8 #042 -> func_000C992C r3=0x4077AC10
```

`func_00041D5C` (#38) corre da linha **4009** à **20767**. **Toda a vida dos
escopos vive lá dentro** — abrem e fecham como parte do trabalho dele.

Logo o enquadramento «um passo fecha e outro precisa» estava mal posto: quando
#38 devolve, os escopos estão fechados **e é isso que devia acontecer**. Os
passos #39 e #40 correm sem abrir nada (medido: nenhuma linha `SCOPE PUSH`
entre as linhas 20767 e 20769), e o #41 pede.

E o #42 recebe `r3=0x4077AC10` — o objecto que o lookup devolveu (E58). O B71
segue com ele.

## O `param_3`, decodificado

O E48 tinha visto que as chamadas a `vt[0x48]` dentro de `FUN_0024e198` estão
atrás de `if (param_3 != 0)`. O valor vem de `FUN_0024f028`:

```c
uVar3 = vt[0x48](gestor);                    /* contexto a' ENTRADA */
...
uVar5 = vt[0x48](o mesmo gestor);            /* contexto AGORA */
uVar1 = (int)(uVar3 ^ uVar5) >> 0x1f;
FUN_0024e198(uVar3 + 0x1c4, param_1, ((uVar1 ^ uVar3 ^ uVar5) - uVar1) - 1 >> 0x1f);
```

Com `uVar3` e `uVar5` do mesmo sinal — que é o caso normal, são ponteiros de
heap — `uVar1` é 0 e a expressão reduz-se a `(uVar3 ^ uVar5) - 1 >> 31`, que
vale **1 se e só se `uVar3 == uVar5`**.

> **`param_3 = 1` quer dizer «o contexto não mudou durante o lookup».**

(Leitura da aritmética decompilada, verificada para o caso de sinais iguais.
Não é medição.)

## E é aí que o desenho se parte

O jogo faz o refresh dos índices de classe **só se o seu contexto estiver
estável**. É um guard razoável — mas vigia o gestor de `PTR_DAT_0053eea4+0x58`,
que o E53 mostrou **não** ser nenhuma das fábricas por-tag e **não** passar
sequer por `func_0039E5A8`.

```
guard vigia:   gestor 0x53EEA4+0x58   -> nao vazio -> param_3 = 1
travessia usa: fabricas por-tag       -> VAZIAS    -> NULL
```

**O guard passa, e a coisa que ele autoriza não está pronta.** A invariante
implícita — *«se o meu contexto está estável, as fábricas por-tag têm
contexto»* — não se verifica na nossa corrida.

## Onde isto deixa o defeito

Não é o deref (E48: o jogo também não verifica), não é o fecho (E60: é o
`leave` legítimo), não é ordem entre passos do B71 (aqui: #38 fecha por dentro
e devia). É:

> **as fábricas por-tag deviam estar repovoadas quando o #41 corre, e não
> estão.**

Duas leituras, e é onde a próxima cadeia começa:

1. **falta um repovoamento** entre o fim de #38 e o #41 — algo que na consola
   corre e na nossa não;
2. **o fecho de #38 é largo demais** — fecha fábricas que deviam sobreviver ao
   fim dele, e nesse caso o defeito é de quem decide o conjunto fechado.

## Refutado por medição

1. **«o passo #38 fecha e o #41 precisa — é ordem entre passos»** — o #38 corre
   da linha 4009 à 20767 e a vida inteira dos escopos está lá dentro; quando ele
   devolve, fechados é o estado correcto.
2. **«os passos #39/#40 reabrem os escopos»** — nenhuma `SCOPE PUSH` entre as
   linhas 20767 e 20769.
3. Tudo o que E11–E17 e E19–E60 já refutaram.

## O estado final da cadeia

```
[PROXIMA CADEIA]  porque e' que as fabricas por-tag nao estao repovoadas no #41
func_00041D5C     B71 #38: abre e fecha os escopos (correcto)          (E61)
func_0010F5E8     B71 #41: pede "goGrapplePtActive"                    (E56/E57)
  func_0024F24C / func_0024F1CC   por nome / por id                    (E60)
    func_0024F028   param_3 = (contexto estavel) = 1  <- guard vigia o gestor errado (E61)
      FUN_0024e198    por elemento, fabrica[tag]
        func_0039E5A8   cursor=-1 -> NULL                              (E45/E51)
      func_0024E3D0   *(0x40780850+6) = 0                              (E59)
        func_00263554   "rm_." como endereco                           (E25/E26)
          func_00220284   16 escritas UNCOMMITTED                      (E8)
```
