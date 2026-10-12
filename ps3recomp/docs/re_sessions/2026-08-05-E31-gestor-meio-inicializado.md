# E31 — o gestor está meio inicializado: flag marcado, pool por criar

> **CONCLUSAO INVERTIDA em 2026-08-06 pelo [E34](2026-08-06-E34-o-gestor-nao-e-gestor.md).**
> Nao e' um gestor meio inicializado: `0x400C6B50` **nunca foi um gestor**. Medido com a
> vigia no `vm_write32` correcto -- o slot `+0xD0` nunca e' escrito por ninguem (nem zerado)
> e o `+0x12C` e' escrito pelo alocador `func_00263178`. E' memoria de contabilidade lida
> como gestor.
> **SOBREVIVE:** as 238 passagens (237 com `mgr=0x406387E0`, 1 com `0x400C6B50`), o
> `flag=0x4007FCE8` e o `pool0=0`. **CAI:** a leitura de 'meio inicializado' e a de que
> `+0x12C` seria um flag que mente.


**Data:** 2026-08-05 · **Antecessor:** [E30](2026-08-05-E30-sintese-e-o-que-resolver.md)

Sonda própria da init preguiçosa (`PS3_TRACE_LAZYINIT`, sítio único, agulha
ancorada na etiqueta `loc_00212F38`). **Raiz localizada.**

---

## A medição

238 passagens pelo ponto do flag, numa corrida:

| nº | `mgr` | `flag` = `*(mgr+0x12C)` | `pool0` = `*(mgr+0xD0)` | init corre? |
|---|---|---|---|---|
| **237** | `0x406387E0` | `0x407735F0` | `0x40773510` | não |
| **1** | `0x400C6B50` | `0x4007FCE8` | **`0x00000000`** | **não** |

E a seguir, na mesma corrida:

```
[POP212] #238 mgr=0x400C6B50 off=0xD0 pool=0x00000000 -> ret=0x2F725F70 "/r_p" <ENVENENADO>
```

## O que isto diz

**O flag está marcado e o pool não existe.**

`func_00212ED0` chama a init preguiçosa (`func_0022831C`) só quando
`*(mgr+0x12C) == 0`. Aqui o campo vale `0x4007FCE8` — não é zero, logo a init é
**saltada** — e `*(mgr+0xD0)` continua nulo.

O gestor `0x400C6B50` está **parcialmente inicializado**: o campo que serve de
guarda foi preenchido, o slot do pool 0 não.

## O `+0x12C` não é um booleano

`0x4007FCE8` é um endereço, não um `1`. E é **exactamente** o valor que o E13
mediu como `base` da tabela de pools:

```
[POOLIDX] MAU pool=0x00000005 slot=0x4007FCE8 base=0x4007FCE8 idx=0 ...
```

Nos gestores sãos o campo vale `0x407735F0`, com `pool0 = 0x40773510` — vizinho,
como se esperaria de duas alocações da mesma init.

Ou seja: `+0x12C` é um **ponteiro para a tabela de pools**, e o código usa
"não-nulo" como sinónimo de "já inicializado". Num gestor onde a tabela foi
criada mas os slots não foram preenchidos, essa guarda mente.

Isto também fecha o E13 por outro lado: o `pool=5` daquela outra família vem de
ler `*(0x4007FCE8)`, a célula 0 da mesma tabela meio-criada.

## A cadeia, com a raiz no sítio certo

```
gestor 0x400C6B50 fica com *(+0x12C) preenchido e *(+0xD0) por preencher   <- RAIZ
func_00212ED0   flag != 0  ->  salta a init preguicosa
                pool = *(mgr+0xD0) = 0
func_00263554   trata o endereco 0 como pool: head = *(4)
                *(0) contem "/wad/r_perma.wad_ps3"   (bug (A) do E30)
                devolve "/r_p"
func_0040FD64   devolve-o como objecto alocado
... seis niveis abaixo: 16 escritas UNCOMMITTED em func_00220284
```

## Isto é bug do jogo ou do nosso recomp?

**Ainda não sei, e não vou fingir que sei.** O que está estabelecido:

- num gestor, a tabela existe e o slot 0 não;
- 237 passagens envolvem um gestor são; **uma** envolve este;
- o guard `!= 0` não distingue "tabela criada" de "pools criados".

Se `func_0022851C` (que faz `*(manager+0xD0) = resultado`,
`ppu_recomp_000.cpp:507641-507649`) tivesse corrido para `0x400C6B50`, o slot
não estaria nulo. Falta medir **se correu** — é a última pergunta desta cadeia,
e distingue as duas leituras:

- **correu e o store não teve efeito** → suspeita recai no lift (store perdido,
  fallthrough, TOC errado);
- **nunca correu para este gestor** → suspeita recai na ordem de inicialização,
  e a pergunta passa a ser quem devia chamá-la.

## Refutado por medição

1. **«a init preguiçosa corre e não cria o pool»** — não corre: `flag != 0`,
   `init_vai_correr=0`.
2. **«o gestor do ponto do flag é outro que não o do pop»** — é o mesmo,
   `0x400C6B50`, linhas consecutivas.
3. **`+0x12C` ser um booleano de inicialização** — é um ponteiro para a tabela.
4. Tudo o que E11–E17 e E19–E30 já refutaram.

## Próximo (E32)

1. **Sonda em `func_0022851C`**: que gestores inicializa e com que resultado.
   Se `0x400C6B50` não estiver na lista, a pergunta muda de "o store falhou"
   para "a chamada não aconteceu".
2. (A) do E30 — quem escreve o path no endereço 0 — continua em aberto e
   precisa de uma vigia validada **no mesmo regime** (E29).
3. `*(r30+0x460C)` em `func_00242C94` — o caminho declarado para o menu.
