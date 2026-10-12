# Fase 9, Plano 09-01 — Tarefa 1: fronteiras do bisect + determinismo

## Determinismo

Repeti a suspeita levantada na sessão de planeamento
(`2026-07-31-dois-binarios-identicos-veredictos-opostos.md`): `boot_gow2.pre_v3` e
`boot_gow2.pre_v4` são **byte-idênticos** (MD5 `ef5f53ef71f1eb3f6230a2ff3ac55dad`, `cmp`
sem diferença) mas tinham veredictos opostos registados.

**3 corridas de `boot_gow2.pre_v3`** (`/tmp/fase9_determinismo.tsv`, `smoke_chain_gate.sh
--bin boot_gow2.pre_v3 3`):

| run | st620 | startseq | thr_end | r_perma | elo_stopped | class |
|---|---|---|---|---|---|---|
| 1 | 11 | 2 | 1 | 1 | nenhum | OK |
| 2 | 11 | 2 | 1 | 1 | nenhum | OK |
| 3 | 11 | 2 | **0** | **0** | AUTO_LOAD (thr_end) | **REGRESSAO** |

**Discordância real: 2 OK, 1 REGRESSAO no mesmo binário.** A corrida 3 NÃO é um artefacto de
medição cortada — `log_lines=3599` (na gama saudável, run1/run2 têm ~4120-4140) e
`StartSeq=2` (chegou ao 2º movie tal como as outras) — só o `thr_auto_load() end` não
disparou dentro dos 90s de timeout. Nas corridas OK, esse print aparece a ~12 linhas do
fim do log (linha 4110/4122 e 4125/4137) — ou seja, **disparar ou não é uma corrida contra
o relógio do timeout**, não uma diferença estrutural.

Segunda leva de confirmação (`/tmp/fase9_determinismo_2.tsv`, mais 3 corridas):

| run | st620 | startseq | thr_end | r_perma | elo_stopped | class |
|---|---|---|---|---|---|---|
| 1 | 11 | 2 | 1 | 1 | nenhum | OK |
| 2 | 11 | 2 | 1 | 1 | nenhum | OK |
| 3 | 11 | 2 | 1 | 1 | nenhum | OK |

Confirmação do lado mau, produção `boot_gow2` (`/tmp/fase9_producao_confirma.tsv`, 3
corridas):

| run | st620 | startseq | thr_end | r_perma | elo_stopped | class |
|---|---|---|---|---|---|---|
| 1 | 11 | **1** | 0 | 0 | 2o movie (StartSeq) | REGRESSAO |
| 2 | 11 | **1** | 0 | 0 | 2o movie (StartSeq) | REGRESSAO |
| 3 | 11 | **1** | 0 | 0 | 2o movie (StartSeq) | REGRESSAO |

### Veredicto: **NAO-DETERMINISTICO** (mas nao 50/50 — raro, e qualitativamente diferente do lado mau)

Somando esta sessão à da véspera: `pre_v3` deu **10 OK em 11 corridas** (a véspera mediu
5/5; hoje 5/6). A falha isolada de hoje é um *near miss* no fim da cadeia (chegou a
`StartSeq=2`, só faltou o último print antes do kill por timeout) — diferente,
qualitativamente, da produção, que fica presa **muito mais cedo** (`StartSeq=1`, nunca
chega a 2) e reproduz isso **100% das vezes** (6/6 combinando as duas sessões).

Conclusão prática para o Plano 09-02: **existe non-determinismo real, mas raro
(~1/11 ≈ 9%) e concentrado num ponto de corrida-contra-o-relógio no fim da cadeia — não
invalida a hipótese de "existe um commit" (a produção é consistentemente diferente, não só
ocasionalmente diferente), mas invalida usar 1 corrida só por passo do bisect** — que é
exactamente a correcção já feita no `09-CONTEXT.md` (mínimo 3 corridas, veredicto por
maioria) e que `bisect_verdict.sh` (Tarefa 2) implementa.

## Confirmacao dos extremos

### Layout dos dois extremos difere -- confirmado

`git worktree add /tmp/ps3recomp_bisect09_good 8648805` e
`git worktree add /tmp/ps3recomp_bisect09_bad 4e815cf`.

| | `8648805` (GOOD esperado) | `4e815cf` (BAD esperado) |
|---|---|---|
| `games/gow2/build_macos.sh` | **AUSENTE** | presente |
| `games/gow2/CMakeLists.txt`/build moderno | ausente (só scripts Windows-era: `build_boot_gow.sh` referencia `/c/Users/.../gow2_work`, `.exe`) | presente |
| raiz `CMakeLists.txt` (motor) | presente | presente |

`build_macos.sh` só entra na árvore em `d039469` ("subtree(gow2): puxa a leva de
2026-07-25 do espelho"), um dos 6 merges da janela — confirmado por
`git log --diff-filter=A 4e815cf -- games/gow2/build_macos.sh` apontando exactamente para
`d039469`. **`8648805` esta ANTES desse import** — o mirror `games/gow2/` que ele carrega
e' de uma epoca anterior ao pipeline macOS actual.

### Achado adicional (nao previsto no 09-CONTEXT.md nem no Plano 09-01): `8648805` está numa linhagem paralela à do lifter macOS, não é reconstruível de forma direta

Tentei construir um candidato real em `8648805` por dois caminhos, e ambos falharam por
razões que revelam algo sobre a topologia da janela:

**Caminho 1 — RELINK (so' a lib runtime de `8648805`, lift de HOJE `recomp_macos_v3`):**
`cmake -B build-macos ... && cmake --build build-macos` (127 objectos, sem erros, ~poucos
segundos) + `PS3_ENGINE_ROOT=/tmp/ps3recomp_bisect09_good OUT=./boot_gow2_fase9_good
./build_macos.sh recomp_macos_v3`. **Falhou a linkar**: `Undefined symbols: _ppu_timebase_now`.
Motivo: `ppu_timebase_now` (o nome que o LIFTER chama) só é definido pelo commit `e441f37`
("fix(runtime): define ppu_timebase_now"), que está **dentro** da janela — `8648805`
(o início da janela) nunca podia tê-lo. Contornei com um shim de compatibilidade **so' para
esta prova** (`/tmp/fase9_shim_timebase.c`, forwarding de uma linha para
`ps3_timebase_now()` — o MESMO corpo que `e441f37` acrescenta 2h depois na janela, sem
mudar semântica) e o link passou. Medido (`smoke_chain_gate.sh --bin boot_gow2_fase9_good
3`, `/tmp/fase9_good_relink_verify.tsv`): **3/3 REGRESSAO** (`StartSeq=1`, igual à
produção) — **NAO o GOOD esperado**.

Isto **não** significa que `8648805` seja mau. Significa que o método RELINK (variar só o
runtime, fixar o lift de hoje) está **confundido** aqui: o lift de hoje já incorpora TODAS
as correcções do lifter feitas ao longo da janela inteira (incluindo correcções que tocam
directamente o caminho `thr_auto_load` e vizinhança, e a correcção `--raw`/jump
tables/clang de `145fe58`). Testar "runtime de `8648805` + lift de HOJE" não corresponde a
nenhum estado real que alguma vez existiu — é uma mistura artificial, e o resultado
(REGRESSAO) não pode ser atribuído ao runtime de `8648805`.

**Caminho 2 — RECONSTRUCAO-COMPLETA (relift PROPRIO de `8648805`, `RELIFT=1`):**
`RELIFT=1 PS3_ENGINE_ROOT=/tmp/ps3recomp_bisect09_good OUT=./boot_gow2_fase9_good_full
./build_macos.sh recomp_macos_fase9_good` — regera o lift com o `tools/ppu_lifter.py`
**do próprio commit** `8648805` contra o `EBOOT.ELF`/`functions.json` estáveis de hoje
(51726 funções, ~0s de lift — rápido, confirma o custo "~16s" do `09-CONTEXT.md`). **Falhou
a compilar**: `'__declspec' attributes are not enabled` — o preâmbulo gerado por
`8648805` usa `__declspec(thread)` **incondicional** (sem ramo `#elif defined(__APPLE__)`)
para `g_trampoline_fn`. Isto é anterior à correção "output volta a compilar em clang"
(`145fe58`, 25 Jul 14:46) e à correção do TLS Apple-safe. Contornei a primeira falha com
`-fms-extensions -fdeclspec` (flags de COMPATIBILIDADE DE COMPILADOR, não mudança de
código-fonte, e as 7 chunks compilaram limpo) — mas o LINK final falhou de forma
**irrecuperável por flags**: `Undefined symbols: _g_trampoline_fn`. O `runtime/ppu/ppu_loader.cpp`
de `8648805` já tem o ramo `#elif defined(__APPLE__)` correcto (`thread_local`), mas a
**declaração gerada pelo lifter** (no preâmbulo do `.cpp` liftado) continua
incondicionalmente `__declspec(thread)` — os dois lados (definição real vs. declaração
liftada) usam modelos de TLS incompatíveis no Itanium ABI da Apple (wrapper `__ZTW...`
vs. acesso directo), e isso **não se contorna com flags de compilador** — precisa do texto
gerado mudar, o que só uma versão posterior do lifter faz.

**Isto é uma incompatibilidade genuína, não um problema de metodologia**: `8648805`
literalmente não produz um binário linkável em macOS/clang por nenhum dos dois caminhos
testados, com o lifter/runtime tal como estavam nesse commit exacto.

### Porque isto acontece: `8648805` está numa ramificação paralela ao lifter macOS

`git merge-base --is-ancestor 145fe58 8648805` → NAO. `git merge-base --is-ancestor
8648805 145fe58` → também NAO. As duas linhas só convergem em
`git merge-base 8648805 145fe58` = `664a4c7` (24 Jul 09:47:54, "feat(trace): nucleo
ps3_trace"). Ou seja: `8648805` (o boundary GOOD escolhido) e `145fe58` (a correcção que
torna o lift compilável em clang) divergiram de um ancestral comum bem mais cedo e só se
reencontram dentro da própria janela, via um dos 6 merges (`git log --merges --oneline
8648805..4e815cf`: `88dc92a`, `921a215`, `d039469`, `2321757`, `07fe030`, `0c6a01b`) —
`8648805` está na ramificação de fiabilidade/testes (`fase0`/`fase1`/`bringup`,
confirmado pela própria mensagem do commit: "docs(fase1): corrige citacoes..."), **não**
na ramificação activa do porte GoW2/macOS.

### Consequência para o oráculo e para o Plano 09-02

1. **Não invalida o bisect** — a maioria dos commits da ramificação
   fiabilidade/testes entre `8648805` e o ponto de convergência não toca em nada
   relevante para o build do GoW2 (nem `games/gow2/`, nem `tools/ppu_lifter.py`, nem
   `runtime/`+`libs/`) e classifica **HERDA** (Tarefa 2) — herda o veredicto do pai sem
   construir nada. O bisect nunca precisa de "provar" `8648805` em si; usa-o como axioma
   (`git bisect good 8648805`), apoiado na evidência histórica já medida
   (`boot_gow2.wip`, 17:47, adjacente a este commit).
2. **Mas junta um terceiro nível de falha ao oráculo**: além de HERDA/RELINK/
   RECONSTRUCAO-COMPLETA, um commit cujo `tools/ppu_lifter.py` gera `__declspec(thread)`
   incondicional (pré-`145fe58`, se algum desses commits vier a ser testado directamente
   pelo bisect dentro da ramificação lifter) **não é recuperável por flags de compilador**
   — o link falha por incompatibilidade genuína de modelo de TLS. `bisect_verdict.sh`
   trata isto como `exit 125` (skip), nunca como "bad" — é uma limitação de portabilidade
   histórica do lifter, não um sinal sobre a regressão.
3. **A confusão do RELINK-contra-lift-de-hoje é real e teria produzido um falso "bad"
   generalizado** se `bisect_verdict.sh` usasse sempre RELINK: por isso o nível 2
   (RELINK) só e' válido quando o `tools/ppu_lifter.py` do commit sob teste gera bytes
   idênticos ao já cacheado (Tarefa 2) — nunca assumido por proximidade temporal.

## Oraculo provado

`bisect_verdict.sh` (Tarefa 2) foi corrido de ponta a ponta (classificacao -> worktree ->
build -> medida -> veredicto) sobre `e441f37` (um commit REAL da janela, nivel RELINK por
diff de ficheiros mas escalado para RECONSTRUCAO-COMPLETA porque o seu `tools/ppu_lifter.py`
difere do lifter de referencia -- ver "RELINK nunca e valido nesta janela" abaixo):

```
$ ./bisect_verdict.sh e441f37
...
run1  st620=0 ... thr_end=0 ... elo_stopped=intro (st620) class=REGRESSAO
run2  st620=0 ... thr_end=0 ... elo_stopped=intro (st620) class=REGRESSAO
run3  st620=0 ... thr_end=0 ... elo_stopped=intro (st620) class=REGRESSAO
elo_stopped=nenhum em 0 de 3 (limiar: 2)
bisect_verdict: e441f37 -> bad
$ echo $?
1
```

Exit code 1 (bad) -- correcto para o contrato do `git bisect run`. O pipeline completo
correu sem intervencao manual: `git worktree add`, `cmake`+`cmake --build` da lib runtime,
relift com o `tools/ppu_lifter.py` DESTE commit contra o `EBOOT.ELF`/`functions.json`
estaveis, `apply_all_patches.sh` (patches de hoje, mantidos constantes -- ja' refutados
como causa pelo Plano 08-02), compilacao dos 7 chunks, link, `smoke_chain_gate.sh --bin`
(3 corridas), limpeza do worktree e do lift temporario.

`e441f37` fica preso na **intro** (`st620=0`), mais cedo do que a producao
(`StartSeq=1`) -- nao e' uma contradicao: `e441f37` e' um commit intermedio da janela,
ANTERIOR a correcoes do lifter que vem depois dele no `--first-parent`
(`1c30e12` "truncated-bounds repair", `34e5bab` "restaura geracao de ppu_stubs.cpp",
`4e815cf` "instrumenta os quatro ppu_res_*"). Um estado intermedio pode legitimamente
falhar MAIS cedo do que o estado final -- o que importa para o bisect e' so' o criterio
binario `thr_end>=1` (nunca dispara aqui), nao O ELO exacto onde para.

### RELINK nunca e' valido nesta janela -- achado que corrige o custo estimado

Comparei `tools/ppu_lifter.py` do FIM da janela (`4e815cf`) contra o HEAD de hoje: **104
linhas de diff, NAO identico**. Ou seja: mesmo o commit mais recente da janela tem um
lifter diferente do de hoje (mais trabalho aconteceu depois, incluindo a Fase 1
"emissões do lifter"). Isto significa que a checagem de seguranca do nivel RELINK
(`cmp -s` contra o lifter de referencia) **escala SEMPRE para RECONSTRUCAO-COMPLETA**
para qualquer commit desta janela especifica -- o nivel RELINK barato (~2min, so' rebuild
da lib + link) nunca se aplica aqui na pratica, apesar de estar implementado e correcto
(seria usado numa janela onde o lifter nao mudasse). O custo real por commit
NAO-HERDA desta janela e' o de RECONSTRUCAO-COMPLETA -- medido nesta prova em
**~1m40s** (23s relift+patches, ~22s compilar 7 chunks, poucos segundos de link, e depois
3 corridas de medida a ~30s cada porque a REGRESSAO e' detectada cedo na intro -- corridas
que chegam mais longe, como as de `pre_v3`, custam ate' aos 90s do timeout cada). Isto e'
MUITO mais barato do que os "30-40min" pessimistas do `09-CONTEXT.md` -- ver a Tarefa 3
para a auditoria completa.

Para o lado bom (`8648805`), nao ha' candidato buildable por nenhum dos dois caminhos
(RELINK confunde-se com o lift de hoje -- medido REGRESSAO 3/3 mesmo sendo o lado GOOD,
ver acima; RECONSTRUCAO-COMPLETA nao linka por incompatibilidade genuina de TLS,
`_g_trampoline_fn` undefined mesmo com `-fms-extensions -fdeclspec`) -- o oraculo
classificaria este commit como `exit 125` (skip) se alguma vez fosse chamado directamente
sobre ele. Na pratica isto nunca acontece no `git bisect run --first-parent`: `8648805` e'
so' o axioma `git bisect good`, nunca um ponto testado pelo proprio bisect.

## Ficheiros de evidencia

- `/tmp/fase9_determinismo.tsv`, `/tmp/fase9_determinismo_2.tsv` -- 6 corridas de
  `boot_gow2.pre_v3` (5 OK, 1 REGRESSAO-por-timeout).
- `/tmp/fase9_producao_confirma.tsv` -- 3 corridas de `boot_gow2` (produção), 3/3 REGRESSAO.
- `/tmp/fase9_bad_relink_verify.tsv` -- 3 corridas do relink de `4e815cf` + lift de hoje,
  3/3 REGRESSAO.
- `/tmp/fase9_good_relink_verify.tsv` -- 3 corridas do relink de `8648805` + lift de hoje
  (shim de compatibilidade), 3/3 REGRESSAO -- descartado como confundido (ver acima).
- `/tmp/relink_good.log`, `/tmp/relift_good_full.log`, `/tmp/relift_good_full2.log` --
  logs completos das duas tentativas falhadas de reconstruir `8648805`.
