# E57 — o nome: o B71 procura `goGrapplePtActive` no registry de tipos

**Data:** 2026-08-07 · **Antecessor:** [E56](2026-08-07-E56-a-cadeia-corre-dentro-do-B71.md)

Leitura estática de `FUN_0010f5e8` (172 bytes) e `FUN_0024f24c`. Põe um **nome**
no topo da cadeia.

---

## O passo #41 do B71

```c
void _opd_FUN_0010f5e8(void)
{
  ppuVar4 = &PTR_DAT_00541178;
  uVar5 = _opd_FUN_0024f24c(PTR_s_goGrapplePtActive_0053c3e0);   /* <- o nome */
  puVar2 = ppuVar4[-0x1367];
  *(undefined4 *)(puVar2 + 8) = uVar5;                            /* guarda global */
  _opd_FUN_0024c878(uVar5,2);
  ...
}
```

**`"goGrapplePtActive"`.** O B71 pede ao registry de tipos o objecto com esse
nome, guarda o resultado num global (`*(g + 8)`) e usa-o já a seguir.

## E o lookup por nome

```c
undefined8 _opd_FUN_0024f24c(void)
{
  iVar3 = _opd_FUN_002b2ba0();              /* nome -> id; 0 = nao existe */
  if (iVar3 != 0) {
    (*(code *)**(undefined4 **)(**(int **)(ppuVar1[-0x8b5] + 0x58) + 0x48))
              (*(int **)(ppuVar1[-0x8b5] + 0x58));   /* vt[0x48], resultado descartado */
    uVar2 = _opd_FUN_0024f028(iVar3);
    return uVar2;
  }
  return 0;
}
```

`ppuVar1[-0x8b5]` é `0x541178 − 0x8b5·4` = `0x541178 − 0x22D4` = **`0x53EEA4`** —
exactamente o `PTR_DAT_0053eea4` que `FUN_0024f028` consulta (E53). É o mesmo
gestor, e é o que **não** passa por `func_0039E5A8`.

## A cadeia inteira, do nome ao sintoma

```
func_000B71B8      B71, sequencia de boot pos-intro
  func_0010F5E8      #41: pede o tipo pelo NOME
    func_0024F24C      "goGrapplePtActive" -> id (func_002B2BA0)
      func_0024F028      le' o SEU gestor (nao vazio), ramo normal
        FUN_0024e198       travessia; por elemento pede a' fabrica[tag]
          func_0039E5A8      cursor=-1  ->  devolve NULL
        func_0024E3D0      *(NULL + 0x20) = 0   -> indice de classe a zero
          func_00263554      ... "rm_." como endereco
            func_00220284      16 escritas UNCOMMITTED
```

E, quinze linhas antes, `func_0041F924` a fechar os escopos das fábricas
por-tag (E53).

## Porque é que isto importa para a parede [D]

O `CLAUDE.md` descreve a parede [D] como *«registry de shaders — o nested walk
NATURAL do typemap segue aberto»*. Aqui está o **mesmo mecanismo** a ser
exercitado: uma consulta por nome ao registry de tipos, que percorre a árvore e
pede a cada fábrica por-tag o seu contexto.

Ressalva honesta: `goGrapplePtActive` é um objecto de jogo, **não** um shader.
O que converge é o *mecanismo* (lookup por nome → travessia do typemap), não o
conteúdo. Se este lookup falha com os escopos fechados, os do registry de
shaders falhariam pela mesma razão — mas isso é previsão, não medição.

## O resultado partido vai para um global

`*(puVar2 + 8) = uVar5` guarda o retorno, e `FUN_0024c878(uVar5, 2)` usa-o logo.
Ou seja, o produto desta travessia — construído com o índice de classe a zero —
**não fica local**: entra no estado do jogo. Vale a pena medir o que sai daqui,
porque é o que o B71 leva para os passos #42 em diante.

## Refutado por medição

1. **«o topo da cadeia era `func_0024F028`»** — acima dele estão o lookup por
   nome e o passo #41 do B71 (E56 + esta leitura).
2. **«o gestor de `FUN_0024f24c` é outro»** — `0x541178 − 0x22D4 = 0x53EEA4`, o
   mesmo de `FUN_0024f028`.
3. Tudo o que E11–E17 e E19–E56 já refutaram.

## Próximo

1. **medir o retorno de `FUN_0024f24c`** para `goGrapplePtActive` — sonda no
   `*(puVar2 + 8)` do `func_0010F5E8`. Diz se o B71 segue com um objecto
   partido ou com zero;
2. `func_002B2BA0` — o nome resolve para um id válido? Se resolve, o registry
   tem a entrada e o problema é só a travessia;
3. quem chama `func_0041F924` (é método de vtable, sem chamador estático — é
   preciso `ra3` na sonda `SCOPE`);
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
