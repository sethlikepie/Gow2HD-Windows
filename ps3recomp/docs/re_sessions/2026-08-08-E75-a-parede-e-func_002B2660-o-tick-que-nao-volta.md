# E75 — a parede é `func_002B2660`: o tick da aplicação não volta

> **UM ARGUMENTO CORRIGIDO em 2026-08-08 pelo [E79](2026-08-08-E79-o-disjuntor-so-dispara-em-alvo-nao-resolvido.md):**
> «`stuck/FATAL`=0, logo e' bloqueio genuino e nao um icall em ciclo» -- o
> disjuntor `PS3_STUCK_ICALL_LIMIT` **so' corre no ramo do alvo NAO RESOLVIDO**
> (`ppu_loader.cpp:2823-2873`); um icall resolvido a girar para sempre nunca o
> dispara. O zero nao distingue as duas coisas.
> **SOBREVIVE:** tudo o resto -- a localizacao em `func_002B2660`, o `frame=1`, o
> degrau 33 inexistente. E os tres ramos CONVERGEM na mesma chamada final, logo a
> pergunta «qual ramo» valia menos do que parecia.

**Data:** 2026-08-08 · **Antecessor:** [E74](2026-08-08-E74-a-cadeia-nao-e-o-bloqueador.md)

O E74 disse onde **não** procurar. Três análises estáticas em paralelo mais duas
medições dizem onde procurar. **A parede tem nome e é um só ponto.**

---

## O ponto exacto

`func_002B2DD0` (degrau 23) é uma sequência recta de **três chamadas, sem um
único ramo** — confirmado no original e no lift:

```c
void _opd_FUN_002b2dd0(void) {
  _opd_FUN_002b2660();   /* 1a */
  _opd_FUN_000b951c();   /* 2a  = degrau 24 */
  _opd_FUN_002b7188();   /* 3a */
}
```

E a sonda de callsite que **já estava no lift** mede:

```
[B71] func_002B2DD0 #001 -> func_002B2660      <- imprime
                    #002 -> func_000B951C      <- NUNCA
                    #003 -> func_002B7188      <- NUNCA
```

Sem condição entre elas, só há uma leitura: **`func_002B2660` não retorna.**

Toda a sub-cadeia dos degraus 24 → 30 pende deste único elo. Não são sete
problemas; é um.

## O loop principal **não** saiu — está dentro do frame 1

```
[STATE] frame=1  0xFFFFFFFF -> 0x0052F3F0  (code 0x002B2DD0) app=0x00730DF8
```

O `STATE-DISPATCH-PROBE`, que também já existia, mostra o loop principal
(`func_00242C94`, degrau 22) a despachar o **frame 1** para o estado cujo código
é `0x002B2DD0` — o degrau 23.

Ou seja: o degrau 23 **é** o handler de estado do primeiro frame. O jogo entra no
loop, despacha o frame 1, e fica preso no tick.

**Refuta** a leitura de que o loop teria saído sem correr frames — foi a
inferência natural a partir da ordem `22 → 23`, e está errada.

E `stuck/FATAL` = **0**: o disjuntor `PS3_STUCK_ICALL_LIMIT` não disparou. Não é
um icall a bater no limite; é um bloqueio genuíno lá dentro.

## O degrau 33 nunca existiu

`patch_autoload_chain_probes.py` trata a 33.ª entrada da cadeia
(`func_0039E6B4`) como **sonda de controlo**, não como degrau:

```python
if fn == "func_0039E6B4":
    rotulo = "CONTROLO %s (tem de imprimir)" % fn
```

Ela emite `[ALCHAIN] CONTROLO ...`, nunca a string `degrau 33`. Não há uma única
ocorrência de `degrau 33` em todo o lift.

**Medido:** o `CONTROLO` imprimiu **1593 vezes**. Isso faz duas coisas — desmente
o meu «degrau 33 não observado» do E73 (era um facto sobre o instrumento, não
sobre a execução) e **valida a instrumentação**: os zeros dos degraus 24–30 são
legíveis como factos porque o controlo dispara.

## O que os três ramos de `func_002B2660` fazem

```c
obj = *(TOC-0x14A0);
if (obj[0xC] != 0)        vt = *(*obj + 0x30);
else if (obj[0xD] != 0)   vt = *(*obj + 0x34);
else { (*( *(*(TOC-0x14A0+0x14)) + 0xC ))(...); return; }
(*vt)(obj);
(*( *(*(TOC-0x14A0+0x14)) + 0xC ))(...);
```

Os três acabam num despacho virtual do guest. Qual deles corre, e o que faz lá
dentro, é a próxima medição — e é pequena: sonda de entrada em `func_002B2730`
e `func_002B26D4`, mais uma linha a seguir ao `ps3_indirect_call`.

## Correcções que isto força

| ledger | o que cai |
|---|---|
| [E73](2026-08-08-E73-o-jogo-ja-esta-no-loop-principal.md) | «degraus 24–30 **e 33** não aparecem» — o 33 não existe |
| E73 | «o jogo já está no loop principal» **sobrevive e melhora**: está no loop **e** no frame 1 |
| (inferência da análise) | «o loop principal saiu sem correr um frame» — `frame=1`, e está lá dentro |

## Refutado por medição

1. **«os degraus 24–30 são ramos não tomados»** — não há ramo entre o 23 e o 24;
   é bloqueio, num ponto único.
2. **«o degrau 33 não foi alcançado»** — não existe; a sonda de controlo com esse
   índice imprimiu 1593 vezes.
3. **«o loop principal saiu com zero frames»** — `frame=1`, e o degrau 23 é o
   handler que ele despachou.
4. Tudo o que E11–E17 e E19–E74 já refutaram.

## Próximo

1. **qual dos três ramos de `func_002B2660`** — sonda de entrada nos dois
   fragmentos + uma linha após o `ps3_indirect_call`;
2. e depois, o que o despacho virtual chama e onde para;
3. o `PS3_GCM_CB` continua por promover;
4. o intermitente do SPU, parado até reproduzir a pedido (E72).
