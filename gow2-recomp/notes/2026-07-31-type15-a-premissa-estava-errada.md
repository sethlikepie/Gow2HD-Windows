# TYPE15: a premissa estava errada — o tipo lê 0x15, não 0

**Data:** 2026-07-31 · **Medido em 3/3 corridas** com `PS3_TRACE_CB56C_TYPE=1` sobre um
binário de teste dedicado (`boot_gow2_type15_probe`).

## O erro, e é meu

Passei ao executor esta tabela, vinda do relatório da Fase 10:

| | referência | actual |
|---|---|---|
| `idx` | 0x54 | **0x0** |
| `ent` | `0x47D00000` | `0x40637C08` |

E construí sobre ela um raciocínio inteiro: `idx = (tipo << 2) & 0x3FFFC`, logo `idx=0`
implica `tipo=0`, logo "o campo de tipo lê 0 em vez de 0x15".

**Propaguei a tabela sem a verificar contra este lift.** O probe mediu:

```
[CB56CTY] pre  tipo=0x0015 idx=0x54 tab=0x00868D48 ent=0x47D00000
                vt=0x00516D70 ctor_opd=0x0051B300 code=0x0039E794
[CB56CTY] SUMMARY tipo=0x0015 n=1 ent=0x47D00000 code=0x0039E794
```

**`tipo=0x15`. `idx=0x54`. `ent=0x47D00000`.** Exactamente a coluna "referência". Um único
tipo, sem variação, em 3/3 corridas.

A coluna "actual" da tabela não corresponde a este lift — vinha de outro estado, medido
noutra sessão. O raciocínio que construí em cima dela era logicamente correcto e
factualmente vazio.

## O que É real e reproduzível

A **mesma fábrica** (`func_0039E794`, `this=0x47D00000`) é chamada **duas vezes**:

```
[FACTORY] 39E794 leave this=0x47D00000 product r3=0x42F85AE4   <- 1a: produto valido
[FACTORY] 39E794 leave this=0x47D00000 product r3=0x4F72626F   <- 2a: 'Orbo'
```

`0x4F72626F` = `b'Orbo'` — texto ASCII onde devia estar um objecto.

E a aritmética de índice é **idêntica** nas duas chamadas (`+D4=0`, `+44=0`,
`+24=0x47D00400`, confirmado nos dois `[FACTORY] enter`). Logo **não é o índice que muda —
é o conteúdo da memória guest** que essa aritmética resolve.

Um detalhe que descarta uma pista falsa: `desc=0x0FEFFA70` não é lixo. O
`GUEST_STACK_TOP` é `0x0FF00000` (`boot_macos.cpp:117`), portanto é um endereço genuíno da
pilha — o `r1+0x70` que o próprio `CB56C` monta como spec mínima.

## E uma correcção à análise do executor

Ele concluiu que o reparador `ps3_type15_freelist_replenish` não cobre o `'Orbo'`:

> *"`'Orbo'` = `0x4F72626F` — fica fora dessa janela (0x4F < 0x5F)"*

**Não fica.** A condição (`host_gow2_factory.cpp:512`) tem três cláusulas em OR:

```c
if (slot < 0x10000u || slot >= 0x4F000000u || (slot >= 0x5F000000u && slot < 0x7F000000u))
    need = 1;
```

`0x4F72626F >= 0x4F000000` é **verdadeiro** — a segunda cláusula cobre. Ele olhou só para
a terceira.

Isso muda a conclusão: se o `slot` for `'Orbo'`, o reparador **dispara**. Ou dispara e não
resolve, ou o `slot` não é `'Orbo'` e a corrupção está noutro sítio. Nota que o `'Orbo'`
observado é o **produto devolvido** (`r3`), e o `slot` é `vm_read32(head)` — **não são
necessariamente o mesmo valor**, e essa distinção não foi verificada.

## O que fica estabelecido

- **MEDIDO (3/3):** `tipo=0x15`, `idx=0x54`, `ent=0x47D00000` — o despacho por tipo do
  `CB56C` está correcto.
- **MEDIDO (3/3):** a segunda invocação da fábrica devolve `'Orbo'`; a primeira devolve
  produto válido; a aritmética de índice é idêntica.
- **MEDIDO:** `func_00029AF0` tem `entradas=0` — a jump table nunca é atingida, o que
  confirma a análise estática de 07-30 e indica que ela serve outro caminho do jogo.
- **INFERIDO, não observado:** que o slot da free-list foi sobrescrito entre as duas
  chamadas. Consistente com tudo, mas ninguém viu o byte mudar.
- **POR VERIFICAR:** se o reparador dispara, e se o `slot` é ou não o `'Orbo'`.

## A próxima medição, exacta

Dentro de `func_0039E794`, dois `fprintf` que imprimam `vm_read32(vm_read32(this+0x24))`
— o slot — **antes e depois de cada uma das duas chamadas**. Isso decide, sem inferência:

- se o slot muda de válido para `'Orbo'` entre as chamadas;
- se o reparador dispara (e, disparando, porque não resolve).

## Duas lições de método, ambas pagas hoje

1. **Não propagar uma tabela sem a re-medir contra o artefacto actual.** Fiz isso e gastei
   uma investigação inteira num raciocínio bem construído sobre um facto falso. A regra
   que esta sessão já tinha aprendido — *"os zeros de ontem não são os zeros de hoje"* —
   aplica-se também aos não-zeros.
2. **Uma condição com três cláusulas em OR precisa de ser lida inteira.** O executor
   avaliou a terceira e concluiu sobre o conjunto.
