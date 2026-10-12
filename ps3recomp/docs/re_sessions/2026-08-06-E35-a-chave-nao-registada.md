# E35 — a chave não está registada, e o lookup devolve lixo em vez de falhar

> **CONCLUSOES REFUTADAS em 2026-08-06/07 pelos [E36](2026-08-06-E36-indice-sem-limite.md)
> e [E38](2026-08-06-E38-o-indice-muda.md).** O lookup `func_0039D3C4` **trata o miss**
> (inicializa `r8=0` e devolve-o), logo nao devolve lixo; e a chave **esta'** registada --
> rende `idx=2` em 7 de 10 consultas. Nao houve miss nenhum.
> **SOBREVIVE:** `tab` e `alvo` identicos nas 238 chamadas, e a lista de chaves com 8 usos
> cada. **CAI:** 'o lookup devolve lixo em vez de falhar' e 'a chave nao esta' registada'.


**Data:** 2026-08-06 · **Antecessor:** [E34](2026-08-06-E34-o-gestor-nao-e-gestor.md)

Sonda própria dos dois lados da mesma vcall de resolução (`PS3_TRACE_MGRLOOKUP`,
sítio único, agulha de 10 linhas verificada). **238 chamadas, resposta limpa.**

---

## A medição

```
#238  key=0x40780850  tab=0x400C6210  alvo=0x0039D3C4  -> mgr=0x400C6B50  <MAU>
#1    key=0x407730F0  tab=0x400C6210  alvo=0x0039D3C4  -> mgr=0x406387E0
#2    key=0x4077312C  tab=0x400C6210  alvo=0x0039D3C4  -> mgr=0x406387E0
```

| campo | 237 sãs | a má |
|---|---|---|
| `tab` (objecto da tabela) | `0x400C6210` | **igual** |
| `alvo` (função de lookup) | `0x0039D3C4` | **igual** |
| `key` | `0x4077…`, `0x407A…`, `0x407E…`, **8 usos cada** | **`0x40780850`, 1 uso** |
| `mgr` devolvido | `0x406387E0` (um gestor real) | `0x400C6B50` (memória do alocador) |

**A tabela e a função de lookup são as mesmas.** Não é despacho errado. O que
muda é a chave — e a chave má é usada **uma única vez**, enquanto cada chave sã
reaparece 8 vezes.

## O que isto estabelece

A chave é um **ponteiro de heap** (`0x407xxxxx`), não um id pequeno. A má tem a
forma certa e está na mesma gama das boas — não é lixo óbvio.

O padrão «cada chave registada é consultada 8 vezes; esta aparece uma vez e
falha» diz que **`0x40780850` não está registada na tabela**.

E o defeito próximo é o que o lookup faz com isso: **`func_0039D3C4` devolve
lixo em vez de falhar.** Um lookup sem tratamento de *miss* devolveu memória do
alocador, e o chamador consumiu-a como gestor sem verificar.

## Os dois defeitos, agora separados

**(1) Próximo — `func_0039D3C4` não trata o miss.** Para uma chave não
registada devolve um valor arbitrário em vez de nulo. Se devolvesse nulo, o
chamador partia limpo em vez de propagar seis níveis.

**(2) Raiz — a chave `0x40780850` nunca foi registada.** Vem de
`*(0x00514E80 + 0x14)` — o objecto da parede 1. Ou o registo desse objecto não
aconteceu (ordem de inicialização), ou o `+0x14` guarda um valor obsoleto.

Nenhum dos dois está fechado. O que está medido é que a tabela e o lookup são os
mesmos nas 238, e que só a chave distingue.

## A cadeia, completa e do princípio ao fim

```
objecto 0x00514E80 (o da PAREDE 1) tem *(+0x14) = 0x40780850
  a chave nunca foi registada na tabela 0x400C6210            <- RAIZ
func_0039D3C4   lookup falha e devolve LIXO em vez de nulo    <- DEFEITO PROXIMO
                -> 0x400C6B50, memoria do alocador
func_00212ED0   le'-a como gestor; *(+0x12C) nao-zero -> salta a init
                pool = *(+0xD0) = 0   (memoria nunca escrita)
func_00263554   trata o endereco 0 como pool: head = *(4)
                *(0) contem "/wad/r_perma.wad_ps3"
                devolve "/r_p"
func_0040FD64   devolve-o como objecto alocado
func_002182A4   this = texto -> divisor 130 -> pool 5
func_002210BC   this final = NULL
func_00220284   16 escritas UNCOMMITTED, stride 0x10
```

Nove níveis, todos medidos, do sintoma que abriu o E8 até uma chave por
registar.

## Refutado por medição

1. **«a tabela de gestores é outra na chamada má»** — `tab` idêntico nas 238.
2. **«o despacho vai para outra função»** — `alvo` idêntico nas 238.
3. **«a chave é lixo evidente»** — é um ponteiro de heap na mesma gama das boas.
4. Tudo o que E11–E17 e E19–E34 já refutaram.

## Próximo

1. **Quem regista as chaves na tabela `0x400C6210`?** Com a vigia
   `PS3_WATCH_W32` — agora no `vm_write32` certo e validada — vigiar escritas na
   tabela e ver quais chaves entram e quando. Se `0x40780850` nunca entra, é
   ordem de inicialização.
2. **`func_0039D3C4`**: ler o lookup e confirmar que não trata miss. Se
   confirmar, é um candidato a fix legítimo (devolver nulo é o comportamento do
   console, não uma máscara) — mas só depois de (1).
3. O flake `FIOSOPEN=14`, ~1 em 6 (E33), continua por explicar.
