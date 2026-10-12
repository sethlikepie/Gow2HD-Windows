# Fase 9: Bissect commit-a-commit da janela - Contexto

**Reunido:** 2026-07-31
**Estado:** Pronto para planeamento
**Modo:** Escrito a partir do funil de eliminação das Fases 6 e 8.

<domain>
## Fronteira da fase

Nomear **o commit** que parte o 2.º `cellVdec StartSeq`, por bissecção automatizada — e,
com ele nomeado, corrigir na origem e repor o boot.

A busca **dirigida** esgotou-se: nove candidatos escolhidos por hipótese foram todos
eliminados. O que resta é busca **exaustiva**, que só agora é viável.
</domain>

<code_context>
## O funil, para não repetir nenhum

Nove candidatos eliminados, **todos por medição, nenhum por inferência**:

| # | candidato | fase | como caiu |
|---|---|---|---|
| 1 | `truncated-bounds repair` | 6 | A/B: **adiciona** 78 funções; e o lift `v3` nem o tinha |
| 2 | `invalid_instructions` | 6 | no-op — o build invoca o lifter **sem `--config`** |
| 3 | `strip_block` | 6 | o `END_MARKER` já estava em produção antes do 1.º binário da janela |
| 4 | TOCFIX | 6 | reversão cirúrgica + rebuild + medição: `REGRESSAO` idêntica |
| 5 | `ctx->lr` explícito | 6 | idem (revertidos em combinação, 9 sítios em `func_00147038`) |
| 6 | sub-emissão da cadeia `CD7B4` | — | o lift que **falha** tem 19 chamadas; o que funciona tem 8 |
| 7 | perda de 2855 funções | — | **inválido**: comparava dois lifts que ambos falham |
| 8 | os 89 patches | 8 | lift limpo + patches de hoje reproduz **exactamente** a falha |
| 9 | runtime `f6708cb` (spinlock/atomics) | 8 | revertido em worktree isolado, relink, medido: `REGRESSAO` idêntica |

**A medição que mais restringe** (Fase 6): o corpo de `func_00147038` (`thr_auto_load`) e a
sua vizinhança de 3 níveis são **byte-a-byte idênticos** entre o lifter do último binário
OK e o de hoje.

## A janela — REVISTA a 2026-07-31, com repetição

A tabela original tinha uma entrada inválida. Estes são os extremos **re-medidos com
repetição**, e são os únicos em que o bisect se deve apoiar:

| binário | corridas | veredicto |
|---|---:|---|
| `boot_gow2.pre_v3` | **5** | **OK** (`thr_end=1` em 5/5) |
| `boot_gow2` (produção) | **3** | **REGRESSAO** (`thr_end=0` em 3/3) |

`boot_gow2.pre_v4` **é byte-idêntico ao `pre_v3`** (md5 `ef5f53ef71f1eb3f6230a2ff3ac55dad`,
confirmado por `cmp`). A linha que o dava como `REGRESSAO` era uma corrida cortada — 813
linhas contra as 11 000+ que o mesmo ficheiro produz consistentemente. **Ignorar essa
linha.** O `boot_gow2_relift_test` (`StartSeq=0`, 3176 linhas) é o próximo suspeito do
mesmo artefacto e deve ser re-medido antes de servir de âncora.

Janela de commits proposta pela Fase 8: **`8648805..4e815cf`** (25 Jul 17:53 → 26 Jul
02:59), **197 commits**.

⚠️ **A delimitação pelo lado bom continua incerta**, mas por outra razão: as mtimes de
`pre_v3`/`pre_v4` vêm do mesmo backup-por-rename, logo não datam a geração. O
`boot_gow2.wip` (25 Jul 17:47) e o `boot_gow2.rdy0_ref` (24 Jul 21:56) são as âncoras com
data fiável — mas **ambos precisam de ser re-medidos com repetição** antes de servirem de
`git bisect good`. Se o bisect não convergir,
**suspeitar da janela antes de suspeitar do método**.

## O que torna isto viável agora (e não era antes)

1. **`smoke_chain_gate.sh`** (Fase 7) devolve rc=0/1 a partir dos elos da cadeia e
   **nomeia o elo que falha**. É o critério automático que o `git bisect run` exige.
   Provado nos dois sentidos: `boot_gow2` → rc=1 ("2o movie (StartSeq)");
   `boot_gow2.pre_v3` → rc=0.
2. **Relink barato** (Fase 8): para commits que não tocam o lifter, basta reconstruir
   `libps3recomp_runtime.a` e relinkar contra os `.o` já existentes — segundos, não os
   30-40 min do build completo.
3. **`bisect_regression.sh --bin`** mede um binário já construído sem rebuild.
4. **`PS3_PATCH_DIR` configurável** (Fase 8) — se um passo precisar de patches de outra era.

## Custos medidos

| operação | custo |
|---|---|
| lift completo | ~16 s |
| rebuild só do runtime + relink | **segundos a ~2 min** |
| build de um chunk | ~8 min |
| build completo | ~30–40 min |
| uma corrida de boot | ~2 min |
| gate de 6 corridas | ~12 min |

**~8 passos de bisect** (log₂ 197). Se cada passo for relink + 1 corrida, são ~40 min.
Se um passo exigir build completo, salta para horas — por isso o script de veredicto tem
de escolher o caminho de build **mais barato que seja válido** para cada commit.
</code_context>

<decisions>
## Decisões já tomadas

- **O critério é o `smoke_chain_gate.sh`**, não o `st620`. Foi medir pelo `st620` que
  deixou esta regressão passar quatro dias.
- ~~Uma corrida por passo do bisect, não seis.~~ **ERRADO — corrigido a 2026-07-31.**

  Escrevi isto e está errado, com prova. Ver
  `../gow2-recomp/notes/2026-07-31-dois-binarios-identicos-veredictos-opostos.md`.

  A tabela dos nove binários foi feita com **uma corrida cada**, e uma delas mentiu: o
  `boot_gow2.pre_v4` ficou registado como `REGRESSAO` (813 linhas) quando é
  **byte-idêntico** ao `pre_v3`, que dá 11 000+ linhas e `OK` em **5 de 5** corridas. Era
  uma corrida cortada, não uma corrida falhada.

  **Regra corrigida:**
  - **mínimo 3 corridas por passo do bisect**, com veredicto por maioria;
  - **uma corrida anormalmente curta é inválida, não é um "mau"** — re-correr. O
    `smoke_relift_equiv.sh` do marco v1.0 já usa essa assinatura como discriminador de
    FLAKE (`log_lines >= 1000` **e** `allocate(0x7D00000)` presente ⇒ flake, não
    regressão). Reutilizar esse critério, não reinventar.

- **O critério é `thr_auto_load end`, NÃO `StartSeq`.** Medido a 2026-07-31:

  | | corridas | `StartSeq` | `thr_end` | `R_Perm` |
  |---|---:|---|---|---|
  | `boot_gow2.pre_v3` (bom) | 5 | 2,2,2,2,2 | **1** ×5 | 199 ×5 |
  | `boot_gow2` (produção, mau) | 3 | **0,1,0** | **0** ×3 | 0 ×3 |

  O `StartSeq` **oscila entre 0 e 1 no lado mau**; o `thr_end` é estável nos dois lados.
  Um bisect que decidisse por `StartSeq` teria ruído; por `thr_end` não tem.
- **`git bisect run` com script**, não bisect manual. 197 commits e ~8 passos é
  exactamente onde o erro humano entra.
- **A correcção continua a ser na origem.** Nomear o commit não autoriza revertê-lo em
  bloco se ele trouxer coisa boa junto — o que se corrige é o defeito, não o commit.

## À discrição de quem implementa

- Como tratar commits que não compilam (`git bisect skip` é o mecanismo próprio).
- Se bissectar no `ps3recomp` (motor) ou também no `gow2-recomp` — a janela tem commits
  nos dois, e o binário depende de ambos.
- O formato do log do bisect.
</decisions>

<specifics>
## Ideias específicas

### O script de veredicto é a peça crítica

Tem de decidir, por commit: **que build é preciso?**

- toca só `runtime/` ou `libs/` → rebuild da lib + relink (segundos)
- toca `tools/ppu_lifter.py` → re-lift (~16 s) + build dos chunks (caro — mas só alguns
  commits da janela tocam o lifter)
- toca só docs/planos → **nem precisa de build**: o veredicto é o mesmo do commit anterior;
  usar `git bisect skip` ou herdar o resultado

Este último caso é o que salva o orçamento: dos 197 commits, a esmagadora maioria são
docs e planos do próprio marco v1.0.

### Armadilhas já pagas nesta sessão

- **`pgrep -f "boot_gow2"`** casa com o texto dos próprios comandos de espera. Usar
  `pgrep -x boot_gow2` ou comparar PIDs. Custou uma hora a perceber.
- **Matar sempre por PID** (`kill -TERM` → espera → `kill -9`), **nunca `pkill -f`**.
- **`git checkout` de commits antigos** vai mexer no working tree do `ps3recomp`. Usar
  **`git worktree`** (a Fase 8 já o fez com sucesso) para não perturbar o checkout activo
  nem os builds em curso.
- Os `runtime/spu/tests/out_*/spu_recomp.{c,h}` são reescritos por qualquer `ctest` e
  **são versionados** — repor com `git checkout --`, nunca commitar.

### Se não convergir

É um desfecho legítimo e tem de estar previsto. As causas prováveis, por ordem:

1. **Janela mal delimitada** (ver o aviso acima sobre as mtimes) — alargar para trás,
   usando `boot_gow2.rdy0_ref` (24 Jul 21:56, OK) como âncora fiável.
2. **A falha depende de dois commits em conjunto** — o bisect assume um ponto de viragem
   único; se for interacção, ele aponta um inocente.
3. **Não-determinismo** — a Fase 7 já observou flakiness de timing sob carga. Um passo
   ambíguo tem de ser repetido, não aceite.
</specifics>

<deferred>
## Adiado

- A parede TYPE15 e os probes `29AF0`/`CB56C` já construídos.
- Cherry-picks do upstream (#81, #79, #86, #87, #88).
- `grep -i assert` no smoke — lição da issue #75.
- Os 2 `FAILED-fora-PROBE` da PERNA 3 do `accept_relift.sh` (pré-existentes).
</deferred>
