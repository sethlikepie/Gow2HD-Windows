# E88/E89 — o desequilíbrio é por construção: push-all itera os filhos do momento

> **§6 SUPERADO por [E90](2026-09-01-E90-o-oraculo-destravado-e-o-fix-de-raiz-da-parede-F.md).** As medições (§2–§5) mantêm-se; a inferência
> «o console tem o mesmo −1» caiu: o oráculo, destravado, termina o #038 com o conjunto A a 0. A diferença era o nosso
> preenchimento host do ring (F2B), que rebobina o ficheiro e produz um segundo `0x06`. Mantido como registo.

**Data:** 2026-09-01 · **Antecessor:** [E87](2026-09-01-E87-o-push-persistente-nao-e-um-push-e-o-atalho-que-parecia-fix.md)
· Nível: `in-boot` (sondas `PUSHALL`, watches em `TABLE[22]`/`TABLE[4]`/stack de contextos), 3 corridas do E89.

## 1. O que push-all / pop-all fazem (lido, e confirmado pela sonda)

`FUN_0041f700`/`FUN_0041f924` (classe 1) e `FUN_0042af18`/`FUN_0042a0c8` (classe 22):
empurram/despejam o **próprio** gestor e depois **iteram a lista de filhos do objeto**
(`obj+0x80` na classe 1, `obj+0x38` na classe 22) chamando push/pop no gestor da classe
de cada filho. O saldo por gestor é **dirigido pelos dados**: depende de que filhos o
objeto tem *no momento* de cada chamada.

## 2. A medição que fecha o E76 (✔)

Sonda `PUSHALL` (objeto + subtags dos filhos percorridos), corrida inteira:

```
4368 push1 obj=0x42F85330 (raiz R_LglScA) filhos=[]                          <- raiz acabada de criar
4380 pop1  obj=0x42F85330                 filhos=[18 25 32 16 21 23 9 17 15 20 4 3]   <- 12 despejados sem push
4669 push1 obj=0x40638588 (raiz R_PermA)  filhos=[]
4681 pop1  obj=0x40638588                 filhos=[18 25 ... 4 3]
```

Para os subtags 4, 3 e 9: **172 pushes, 174 pops, saldo −2** — um por WAD. O primeiro
negativo é na linha 4380. Não é «push por low16, pop por subtag» (E76): é **push-all com
lista vazia, pop-all com lista cheia**. Os registos que criam os filhos ficam entre os
dois.

## 3. O que persiste e o que não (✔)

No fim do #038: `TABLE[22]` tem `cursor=0` e `stack[0]=R_PermA` (vivo) — **o WAD corrente
persiste**; o stack de contextos de WAD (`0x868C00`) fica com um nível a mais que à
entrada do B71 (174 pushes/173 pops). Os 5 gestores do conjunto B ficam em +1. Os 10 do
conjunto A (onde estão `TABLE[4]` e `0x400D6DA0`) ficam em **−1**, pelo §2.

## 4. E89 — refutação da hipótese «R_LglScA anexado no console» (✔ 2 ON + 1 OFF)

Gate `PS3_E89_LGLSCA_ATTACHED=1` força o bit 4 à entrada de `FUN_000415f0`: o ramo
push+release é saltado (`CA14=0`, o R_LglScA sobrevive) e **nada muda** — `frame=1`,
`VAZIO=6`, `ICALL-BAD=8`, wrap a 6·10⁹. O release do R_LglScA (E87 §2) **não** é a fonte
do desequilíbrio. E `FUN_00041014`, chamada logo a seguir no #038, é «se não anexado,
anexa» — a sequência finalizar→anexar é a do jogo.

## 5. Refutado nesta sessão (não voltar a perseguir)

1. **AUTO_LOAD como fonte do escopo** — a thread só nasce depois do B71 (`FUN_000bb0b0`,
   estado 0 da máquina `PTR_DAT_0053b0a0`), e #042–#054 são *dentro* do B71.
2. **O B71 editado de julho remove chamadas** — o re-lift fiel (`recomp_macos_v4_faithful`,
   51 991 funções, 0 stubs) tem as **mesmas 63 chamadas**; só guardas e HLE-lite foram
   acrescentados. `PS3_B71_FULL_393E0=1` não muda nada.
3. **Stream assíncrono no console deixa o grupo aberto** — o `do/while` de `FUN_002ba76c`
   devolve 1 quando o buffer seca e só 0 no fim; o `while` do #038 espera até ao 0.
4. **Repouso ≠ −1 no console** — o construtor escreve `0xFF` no cursor (`func_002B11B8+0x5A30`).
5. **R_LglScA anexado no console** — §4.
6. **`PS3_SKIP_NULLTYPE` como fix** — E87 §6 (mantém-se).

## 6. O que fica, com precisão

Se o código é o mesmo, o console tem o **mesmo** −1 por WAD nos gestores do conjunto A
— a menos que, no console, as listas de filhos estejam **cheias já no primeiro push**
(isto é, os filhos existam antes do registo `0x13` ser processado) ou os registos
cheguem por outra ordem. Isso é um facto sobre o **conteúdo do stream e a ordem de
registo no console**, não sobre o nosso runtime — e o único instrumento que o mede é
o oráculo RPCS3, que nesta configuração não chega lá (E81).

Sondas: `patch_e88_pushall_children_probe.py`, `patch_e89_lglsca_attached_gate.py`
(gow2-recomp). Diagnóstico honesto que continua a dar frame N: `PS3_E86_KEEP_SCOPE`.
