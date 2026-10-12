# E87 — o "push persistente" não é um push, e o atalho que parecia fix não é

**Data:** 2026-09-01 · **Antecessor:** [E86](2026-09-01-E86-predicoes-manter-o-escopo-aberto.md)
· **Pergunta do utilizador:** *acha o push persistente, quero o fix de raiz*

> **RETRATAÇÃO, no mesmo dia:** a meio desta sessão disse ao utilizador que
> `PS3_SKIP_NULLTYPE=1` «é a resposta» porque dá `frame=5000` sem o E86. **Estava
> errado.** A corrida de mecanismo (§6) mostra que funciona por acidente: a
> corrupção muda de sítio. Fica registado como refutado.

Nível de evidência: `in-boot` onde marcado ✔; `lido no decompilado` nas leituras
estáticas; **nada sobre o console foi medido** — o que o console faz é inferência.

---

## 1. A contabilidade do escopo, por gestor e por sítio (✔)

15 gestores. Ao chegar a #042, **10** estão em `cursor=−1` e **5** em `+1`. A diferença
entre os dois grupos, por `lr`: os 10 recebem `1 PUSH@0x2B0DDC` contra `2 POP@0x2B9CA8 +
1 POP@0x2553D8`. Mas isto **não** é assimetria push/pop do E76: seguindo os registos do
WAD um a um (sonda `DESTROY` = `FUN_002b9c80`, o despacho por tipo de registo):

| registo | handler | efeito em `0x400D6DA0` |
|---|---|---|
| `0x13` #1 | push (`FUN_002b0db0`) | `−1 → 0` |
| `0x00` | — | `0 → −1` |
| `0x13` #2 | push | `−1 → 0` |
| `0x05` | push | `0 → 1` |
| `0x06` #1 | pop (`FUN_002b0d60`, payload classe 1 → pop-all) | `1 → 0` |
| `0x06` #2 | pop | `0 → −1` |
| `0xA01F` (conteúdo) | 167 push / 167 pop | `−1` |

**O stream é LIFO e equilibrado.** `{0x13#1,0x00}`, `{0x13#2,0x06#2}`, `{0x05,0x06#1}`. O
nosso processamento é fiel. O nível que o console mantém tem de vir de **fora** do stream.

## 2. `FUN_000415f0` — o candidato a push persistente, e o que ele é de facto (✔ + lido)

Chamada **uma vez** no #038 (`lr=0x00041FFC`), com o objeto que o #038 procura **por nome**:
`FUN_0003df98()` devolve a string `"R_LglScA"`. Sonda `LGLSC`:

```
p=0x42F84EA0 classidx(+6)=0x0016 ownerslot(+0xA)=0x0000 flags(+8)=0x0020 bit4=clear -> vai empurrar
```

O corpo, com nomes:

```c
flags = p->+8;  p->+8 = (flags & 0xFFC0) | (flags & 0xF) | 3;
if ((flags & 4) == 0) {                      // NAO ANEXADO ao dono (bit 4 = attach, posto por vt[0x18])
    TABLE[classe]->vt[0x40](p+4);            // push-all  ✔ (SCOPE: PUSH 42AF5C, 36 linhas depois)
    TABLE[classe]->vt[0x24](p+4);            // "notifica o dono" -- ver abaixo
}
```

`vt[0x24]` do gestor = `FUN_0039d598` = `dono = instances[*(u16*)(obj+6)]` (**slot do
dono**, o campo dos E74/E77) → `dono->vt[0x24](dono, obj)`. O dono do R_LglScA é
`instances[0] = 0x400C4330` (vtable `0x5174B8`), cujo `vt[0x24]` = `FUN_0039d9d0` =
**`if (vt[0x5c](obj)) vt[0x54](obj)`**, e nessa classe **`vt[0x5c]` = `FUN_0042ca14` =
release**. Medido (`CA14`): `refcnt(+0x2c)=1 → 0` → destrói o R_LglScA → `FUN_0024e718`
drena os *buckets* → `release()` do produto-raiz das entidades (`0x42F85334`, classe 1,
refcount 1, `REL-walker`) → é o produto corrente → **pop**. 36 linhas depois do push.

Ou seja: **`FUN_000415f0` não empurra um escopo persistente. É a finalização/descarte de um
WAD *não anexado*: empurra-o como corrente e pede ao dono que o largue.** O R_LglScA é
o WAD da tela legal — 3 072 bytes, descartável. **Isto é fluxo normal, e provavelmente é
igual no console.**

Refcounts, medidos com watch (✔): o do R_LglScA (`+0x2c`) nasce a 1 na construção e nunca
é incrementado; o único `addref` genérico (`FUN_0039e04c`, `vt[0xD0]`, incrementa o
**pai** em `obj->+0x18`) corre 40× e nunca incrementa (todos com `+0x18 = 0`).

## 3. Onde a sessão chegou, sem chegar ao fix

Depois do R_LglScA, o nosso boot só abre **`r_perma.wad_ps3`** (20 MB; os 169 registos
correm dentro de `FUN_00041494("WAD_R_Perm")`, o último passo do #038, cujo pop-all é
incondicional). O console, pelas strings do próprio #038 (`"Levels"`, `"GBL_Global"`,
`"SND_Globals"`, `"Hero"`, `"HERO_HEAP_SIZE"`), carrega mais coisas — e o valor inicial do
cursor (`+0xC8`) **não é escrito por nenhum construtor** (só push/pop o tocam): nasce com o
que o alocador do jogo (`FUN_00263040`) deixar na memória. No nosso boot nasce a −1.

**O que fica em aberto, dito com precisão:** por que razão, no console, os passos #042–#054
encontram `cursor ≥ 0` nos gestores. As hipóteses vivas são duas e só o oráculo separa:
(a) um WAD/nível é carregado **entre** o R_PermA e o B71 e fica anexado como corrente;
(b) o repouso do console não é −1 (o gestor nasce com `cursor=0` e `stack[0]` válido).

## 4. Os dois gates, com veredicto (✔)

| gate | o que faz | efeito | veredicto |
|---|---|---|---|
| `PS3_E86_KEEP_SCOPE` (E86) | salta o pop-all do #038 | `frame=7000`, `VAZIO` 6→0, `ICALL-BAD` 8→0, wrap sai | **diagnóstico honesto**: muda a variável certa, mas o console faz esse pop |
| `PS3_SKIP_NULLTYPE` (E74) | não escreve `slot_do_dono := *(NULL+0x20)` nos 7 sítios | `frame=5000` 3/3 | **REFUTADO como fix** — ver §6 |

## 5. As 6 escritas de zero são consequência, não causa (✔)

Todas em `func_0024E3D0`/`func_0024E430` (o walker), linhas **5969–6016**; a destruição do
R_LglScA é na **5025**. As vítimas (`0x4077AC10`, `0x40780850`, …) apanham slot 0 porque a
fábrica já estava vazia. O E6 (`PAREDE1-SKIP`) poda o attach de 2 delas em ambos os regimes.

## 6. Por que o `SKIP_NULLTYPE` "funciona" — e por que isso o mata (✔)

Corrida de mecanismo com todas as sondas (a primeira tentativa ficou inválida por um erro
meu no harness — `export "A=1 B=2"` define **uma** variável; refeita com exports certos):

```
VAZIO = 20 928                  (baseline: 6)
[R31SRC] pos-virtual r3=0x00000000
[R31SRC] arg-func_00237CDC r3=0x2F725F70   CTRL=outro      <- ASCII "/r_p"
[EMPTYFAB] fab=0x400D6DA0 cursor=-1 VAZIO->devolve-0        (consulta do #054, igual)
[CA14] release-WAD ... refcnt(+0x2c)=1                       (o WAD morre na mesma)
```

A fábrica continua vazia, o WAD continua a ser destruído, a consulta do #054 continua a
devolver NULL — e o `*(0+0x48)` → pop de free-list passa a devolver **um ponteiro para uma
string de path** (`"/r_p…"`) em vez do TOC. O objeto de ~0x120 bytes é escrito por cima da
string, o rodata fica intacto por acidente, o wrap não trava, o loop roda com lixo e
20 928 consultas vazias. **Não é fix. É a mesma corrupção a cair num sítio inofensivo.**
E é o caso de manual da skill *walking-a-corruption-upstream* (`0x2F725F70` = `/r_p`).

## 7. Refutado nesta sessão

1. **«push e pop desequilibrados no stream do WAD»** — LIFO, equilibrado registo a registo.
2. **«`FUN_000415f0` é o push persistente»** — é a finalização de um WAD não anexado; o
   push que faz é desfeito 36 linhas depois pelo release que ele próprio pede.
3. **«o E6 poda o attach do R_LglScA»** — poda 2 vítimas do walker, não o WAD.
4. **«o TOCFIX / um disjuntor nosso / o TOC errado»** (herdados do E83/E84) — continuam mortos.
5. **«`PS3_SKIP_NULLTYPE` é fix»** — §6. **Minha, retratada.**
6. **«o pop-all de `FUN_000c2e68` é um job diferido»** — é get-or-create numa skip-list;
   o pop é o `vt[0x44]` síncrono a seguir.

## 8. O próximo discriminador (desenhado, não corrido)

O oráculo RPCS3 (`tools/rpcs3_probe`, E80/E81), com breakpoint em **`0x000C992C`** (entrada
do #042): ler o byte `cursor` (`mgr+0xC8`) e `stack[0..1]` (`mgr+0x48..`) de **`TABLE[4]`**
(`*(0x868D58)`) e do gestor `*(*(TOC−0x2080)+0x24)`, e a lista de ficheiros abertos até aí.
Isso separa (a) de (b) do §3 numa corrida — e é a **única** medição que diz o que o console
faz, em vez de inferir. Sem ela, qualquer "fix" é um gate.
