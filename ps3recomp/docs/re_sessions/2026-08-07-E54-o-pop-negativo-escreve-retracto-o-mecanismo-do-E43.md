# E54 — o pop negativo escreve: retracto o mecanismo do E43 (a medição fica)

**Data:** 2026-08-07 · **Antecessor:** [E53](2026-08-07-E53-sobra-a-chamada-o-escopo-foi-desmontado-antes.md)

A ler `FUN_0041f924` (o desmonte do E53) apareceu, no decompilado do original,
um decremento **incondicional**:

```c
cVar1 = *(char *)(param_1 + 200);
if (cVar1 < '\0') { iVar6 = 0;  *(char *)(param_1 + 200) = cVar1 + -1; }
else               { *(char *)(param_1 + 200) = cVar1 + -1;
                     iVar6 = *(int *)(param_1 + 0x48 + cVar1 * 4); }
```

Isto colide de frente com o E43. Fui ao ramo que a minha sonda não cobre.

---

## O ramo que eu não tinha lido

`func_0039DAB0` salta para `func_0039DAF4` quando o cursor já é negativo. E
`func_0039DAF4` é:

```c
void func_0039DAF4(ppu_context* ctx) {
        ctx->gpr[8] = (int64_t)(int32_t)(0);
        vm_write8(ctx->gpr[3] + 0x80, ctx->gpr[10]);   /* ESCREVE cursor-1 */
        ctx->gpr[3] = ppc_rldicl(ctx->gpr[8], 0, 32);  /* devolve 0 */
        return;
}
```

**Escreve.** O pop com cursor negativo devolve 0 *e decrementa na mesma*. O
cursor **pode** ir a −2, −3, …

## Retracção ao E43

**CAI o mecanismo.** O E43 escreveu:

> *«o decremento tem guard de sinal; sai sem escrever quando já é negativo»* e
> *«underflow abaixo de −1 é estruturalmente impossível»*

As duas frases são falsas. O salto não é um guard que aborta — é um desvio para
outro fragmento que faz a escrita e devolve 0.

**SOBREVIVE toda a medição, e a conclusão.** A vigia `PS3_WATCH_STORE` vive no
`heap_w()` e apanha **todas as larguras vindas de qualquer sítio**, incluindo
esta. Em duas corridas sobre `0x400C62D8`:

| escritor | escritas |
|---|---|
| `func_0039DA78` (push) | 349 |
| `func_0039DAB0` (pop, ramo ≥0) | 352 |
| `func_002B11B8` (init) | 2 |
| **`func_0039DAF4` (pop, ramo <0)** | **0** |

e os valores, numa corrida: `0x00` (176), `0x01` (165), `0xFF` (12). **Nunca
`0xFE` nem abaixo.**

Logo: **não houve underflow** — não porque seja impossível, mas porque naquela
fábrica, naquelas corridas, o pop nunca correu com o cursor já negativo.
A conclusão do E43 mantém-se; o argumento que a sustentava não.

## E reproduzi o defeito de instrumento que eu próprio diagnostiquei

A sonda `SCOPE` (E52) está ancorada **depois** do salto, dentro de
`func_0039DAB0`. Os pops que passam por `func_0039DAF4` **não são contados por
ela** — exactamente a mesma cegueira de ramo que a sonda `PRODUCT` tinha e que
eu descrevi no E45.

Consequência concreta: as contagens «~175 push / ~175 pop, equilibradas» que o
E50 publicou para as **outras quatro** fábricas assentam num instrumento cego a
um dos dois ramos do pop. Para `0x400C6210` a vigia cobre tudo e o número é
firme; para `0x403008E8`, `0x40300E80`, `0x400D6DA0` e `0x401002F0` **não está
estabelecido**.

Marcado no E50.

## Porque é que a vigia pôde responder e a sonda não

Vale a pena fixar a diferença, porque decide onde vale a pena pôr instrumentos:

- a **sonda** vive num sítio do código — vê o que passa naquele sítio;
- a **vigia** vive no `heap_w()`, o funil por onde passam `vm_write8/16/32/64` —
  vê o endereço, venha de onde vier.

Instrumentar o **dado** cobre ramos que ainda não se conhece. Instrumentar o
**sítio** só cobre o que já se assumiu.

## Refutado por medição

1. **«o pop sai sem escrever quando o cursor é negativo»** — `func_0039DAF4`
   faz `vm_write8` do valor decrementado.
2. **«underflow abaixo de −1 é estruturalmente impossível»** — é possível; só
   não aconteceu.
3. Tudo o que E11–E17 e E19–E53 já refutaram, **excepto** o mecanismo do E43,
   aqui retractado.

## Próximo

1. **estender a vigia às cinco fábricas** (`PS3_WATCH_STORE` aceita até 8
   moradas) e refazer as contagens de push/pop com um instrumento que não é
   cego a ramos — repõe o que o E50 afirmou sem base;
2. ou, em alternativa, ancorar a sonda `SCOPE` também em `func_0039DAF4`;
3. a pergunta do E53, que continua a ser a que decide o fix: quem chama
   `FUN_0024f028` e porque é que corre depois de `func_0041F924`
   (oito chamadores no dump: `FUN_00021410`, `FUN_0002631c`, `FUN_00038c10`,
   `FUN_0009f2a4`, `FUN_000c72fc`, `FUN_001440e8`, `FUN_0024f1cc`,
   `FUN_0024f24c`);
4. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
