# E34 — parede quebrada: o "gestor" nunca foi um gestor

**Data:** 2026-08-06 · **Antecessor:** [E33](2026-08-06-E33-nao-destrava-mais.md)

Duas medições com uma vigia **finalmente no sítio certo**. A cadeia fecha, e a
leitura dos E31/E32 inverte-se.

---

## Primeiro: a vigia estava no ficheiro errado desde o E16

Existem **duas implementações de `vm_write32`**:

| implementação | onde | quem a usa |
|---|---|---|
| `static inline void vm_write32(uint32_t, uint32_t)` | `runtime/ppu/ppu_memory.h` | runtime e HLE |
| `void vm_write32(uint64_t, uint32_t)` | `runtime/ppu/ppu_loader.cpp:1427` | **o código LIFTADO** |

O lift inclui `ppu_recomp.h`, que declara a segunda. **Instrumentei a primeira**
— por isso as vigias nunca viram uma única escrita do guest, e os únicos
positivos que alguma vez deram vieram de `gow2_midasm_hooks`, outro TU.

Isto explica, de uma vez, a família de zeros falsos dos E16, E17, E22 e E28 —
que o [E29](2026-08-05-E29-a-string-esta-no-endereco-zero.md) já tinha marcado
como suspeita sem saber a causa.

**A vigia nova (`PS3_WATCH_W32`, em `ppu_loader.cpp`) foi validada no mesmo
regime antes de qualquer medição:**

```
[W32] #1 write 0x406388B0 = 0x00000000 ra=func_00228368+0x124   (zera)
[W32] #2 write 0x406388B0 = 0x40773510 ra=func_0022851C+0x544   (poe o pool)
```

Dois escritores no gestor são, como se esperaria: um zera, o outro preenche.

## As duas medições

| alvo | escritas | quem |
|---|---|---|
| `0x400C6C20` = `mgr+0xD0`, o slot do pool 0 | **0** | ninguém |
| `0x400C6C7C` = `mgr+0x12C`, o "flag" | **1**, `= 0x4007FCE8` | **`func_00263178+0x2054`** |

## O que isto obriga a concluir

**`0x400C6B50` nunca foi um gestor.**

- O slot do pool 0 não é "um campo por preencher": **nunca foi escrito por
  ninguém**, nem sequer zerado. No gestor são, `func_00228368` zera-o primeiro.
- O campo que eu li como "flag de inicialização" é escrito por
  **`func_00263178`** — o alocador da free-list, a mesma família do
  `func_00263554`. É contabilidade do alocador, não um gestor.

Ou seja: `func_00212ED0` obtém o gestor por chamada virtual e, **numa chamada em
238**, essa chamada devolve memória que pertence ao alocador. Todo o resto —
`divisor=130`, a "tabela de pools" em `0x4007FCE8`, o `pool=5` — são
interpretações de dados alheios, não campos corrompidos.

Isto **inverte** o E31 e o E32: não é um gestor meio inicializado, é um objecto
que não é gestor nenhum.

## A cadeia, na forma final

```
func_00212ED0   key = *(0x00514E80 + 0x14)          <- o objecto da PAREDE 1
                gestor = vcall(obj, vt+0x50, key)
                devolve 0x400C6B50 -- memoria do alocador, NAO um gestor   <- RAIZ
                le' *(0x400C6B50+0x12C) como "flag": 0x4007FCE8 (nao-zero)
                  -> salta a init preguicosa
                pool = *(0x400C6B50+0xD0) = 0 (memoria nunca escrita)
func_00263554   trata o endereco 0 como pool: head = *(4)
                *(0) contem "/wad/r_perma.wad_ps3"
                devolve "/r_p"
... seis niveis abaixo: 16 escritas UNCOMMITTED em func_00220284
```

O objecto `0x00514E80` é o mesmo da parede 1. **Toda esta cadeia é a parede 1 a
manifestar-se por outra via**, agora que o aborto foi removido.

## Refutado por medição

1. **«o gestor está meio inicializado»** (E31) — não é um gestor.
2. **«a init nunca correu para este gestor»** (E32) — verdade, mas irrelevante:
   nunca deveria correr para memória do alocador.
3. **«`+0x12C` é um flag que mente»** — é um campo do alocador; a mentira está
   em lê-lo como flag.
4. Tudo o que E11–E17 e E19–E33 já refutaram.

## O que resolver

A pergunta é agora **uma só e está no sítio certo**: porque é que a chamada
virtual de resolução do gestor devolve memória do alocador para a chave
`*(0x00514E80 + 0x14)`?

Duas hipóteses, e distinguem-se com uma sonda na chave:

- **chave inválida** — o objecto `0x00514E80` tem `+0x14` com lixo, e a tabela
  de gestores devolve o que calha. Liga-se à parede 1, cujo fix podou um
  dispatch **sem reparar o objecto**.
- **tabela de gestores incompleta** — a chave é válida mas não está registada.

**Não aplicar fix antes disto.** Qualquer poda no pop ou no guard mascara uma
resolução de tipo errada, que é o defeito real.

## Nota de método

O controlo no mesmo regime salvou esta rodada pela segunda vez. O primeiro
controlo (`0x406388B0`, positivo provado pelo `POOLSTORE`) deu **zero** — e foi
esse zero, não a medição, que revelou as duas implementações de `vm_write32`.

> Um controlo que falha num sítio onde a resposta é conhecida vale mais do que a
> medição que ele ia validar.
