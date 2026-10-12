# E51 — os chamadores reais: a fábrica não está partida, o momento é que é outro

> **ENQUADRAMENTO CORRIGIDO em 2026-08-07 pelo [E52](2026-08-07-E52-dois-dos-seis-sao-por-desenho.md).**
> Este ledger (e os E45-E50) tratam os **6** pedidos vazios como 6 instancias do
> defeito. Nao sao. Dos seis: **3** (`func_0024E3D0`) dereferenciam `*(NULL+0x20)`
> e sao a cadeia; **1** (`func_00264BA8`) dereferencia `*(NULL+0x48)` mas ocorre
> DEPOIS do sintoma (rota latente); **2** (`func_0039F0F8`) sao **por desenho** --
> save/restore no nivel exterior, e o valor e' so' comparado, nunca dereferenciado.
> **SOBREVIVE:** toda a medicao -- as 6 leituras com `cursor=-1`, os chamadores
> reais, as contagens, a particao limpa e as cinco fabricas nos dois lados.
> **CAI:** so' a leitura de que os seis sao o bug.

**Data:** 2026-08-07 · **Antecessor:** [E50](2026-08-07-E50-o-lr-nao-e-o-chamador.md)

Sonda `EMPTYFAB` v3: `ra1`/`ra2` de host resolvidos por `dladdr`, ao lado do
`ctx->lr` — para se ver a divergência que o E50 provou, em vez de a assumir.
Instrumento novo em `runtime/ppu/ppu_loader.cpp`: `ps3_dbg_sym()`.

---

## Os chamadores, agora com base

| chamador real (`ra1`) | chamadas | resultado |
|---|---|---|
| `func_0027C6F0+0x54C` | 238 | `ok` |
| `ps3_call_opd+0x1D0` | 15 | `ok` |
| `func_0024E3D0+0x444` | 3 | **VAZIO** |
| `func_0039F0F8+0xC20` | 2 | **VAZIO** |
| `func_00264BA8+0x15C` | 1 | **VAZIO** |

`238+15 = 253` e `3+2+1 = 6`. A partição do E49 mantém-se — **e agora tem
nomes que resistem**.

`func_0024E3D0` é a função da decompilação do E48, dentro de `FUN_0024e198`. O
resgate que o E50 fez por fluxo de controlo está confirmado por medição directa.

## O E50 confirmado numa linha

```
fab=0x400C6210 cursor=-1 VAZIO  ra1=func_0039F0F8+0xC20  lr=0x002B2DE0
```

O `lr` diz `0x002B2DE0` (que o E49 leu como `FUN_002b2dd0`); o `ra` diz
`func_0039F0F8`. **São funções diferentes, na mesma linha, na mesma chamada.**
A retracção do E50 deixa de ser inferência sobre o lifter e passa a observação.

## O resultado que muda a forma do problema

| fábrica | `ok` | `VAZIO` |
|---|---|---|
| `0x400C6210` | 1 | 2 |
| `0x403008E8` | 13 | 1 |
| `0x40300E80` | **225** | 1 |
| `0x400D6DA0` | 1 | 1 |
| `0x401002F0` | 1 | 1 |

**As cinco fábricas aparecem nos dois lados.** `0x40300E80` serve produto 225
vezes e falha uma.

Portanto **nenhuma fábrica está partida, mal inicializada ou esfomeada** — o
E50 já tinha mostrado que todas levam ~175 pushes equilibrados, e agora
vê-se que todas *servem* produto quando lhes pedem na altura certa.

O que resta é só isto: **três chamadores pedem fora da janela em que há
produto, e nunca dentro dela.**

## Nomes novos para puxar

- `func_0024F028+0x8F8` — quem chama `func_0024E3D0` nas três falhas;
- `func_0039F0F8` — vive na vizinhança das funções da fábrica (`0x39DA78`
  push, `0x39E5A8` leitura, `0x39F0F8`), e é chamada **indirectamente**
  (`ra2=ps3_indirect_call`), logo é provavelmente outro método da mesma classe;
- `func_00264BA8+0x15C`, chamada de `func_00237CDC+0x2B4`;
- do lado que acerta, `func_0027C6F0+0x54C` com 238 — vale ler o que ele faz
  **antes** de pedir, porque é isso que os três falhados não fazem.

## Refutado por medição

1. **«as fábricas que falham estão partidas / vazias por defeito»** — as cinco
   servem produto noutras chamadas da mesma corrida; `0x40300E80` 225 vezes.
2. **«o `lr` chegava para nomear o chamador»** — `ra1=func_0039F0F8` contra
   `lr=0x002B2DE0` na mesma linha (confirmação directa do E50).
3. Tudo o que E11–E17 e E19–E50 já refutaram.

## Próximo

1. **ler `func_0027C6F0` à volta de `+0x54C`** — o que ele estabelece antes de
   pedir. É o contraste mais barato que há: 238 acertos contra 6 falhas;
2. `func_0024F028` — porque é que chama `func_0024E3D0` fora da janela;
3. `idx=24444` (E38) e o flake `FIOSOPEN=14` (E33), ainda por explicar.
