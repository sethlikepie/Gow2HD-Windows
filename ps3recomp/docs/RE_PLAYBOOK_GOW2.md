# RE_PLAYBOOK_GOW2 — como atacar uma parede do GoW2 sem forjar o fecho

> **Este documento é método, não promessa de vitória.** Uma sessão que refuta duas
> hipóteses com medição e escreve porquê vale mais do que uma que promete vencer a
> parede. **"Ainda aberto" é resultado válido; um fecho forjado não é.**
>
> Regras absolutas do projecto: `CLAUDE.md` na raiz. Este playbook não as substitui —
> operacionaliza-as. Onde houver conflito, ganha o `CLAUDE.md`.
>
> Sessões reais escritas com este playbook: `docs/re_sessions/`.
> A primeira é
> [`2026-08-02-wall-4-walk-de-tipos.md`](re_sessions/2026-08-02-wall-4-walk-de-tipos.md)
> — parede 4 (`func_0041F700`), três hipóteses refutadas por medição, veredicto **aberto**.

**Para agentes (Claude/Codex/Grok) e humanos:** antes de abrir uma parede do GoW2,
lê as secções 1–6 desta página e escreve o ledger da sessão em
`docs/re_sessions/YYYY-MM-DD-wall-<slug>.md`. Sem ledger, a sessão não existe.

---

## 1. A pergunta de wall — uma frase

Uma parede começa por **uma pergunta que uma medição pode responder com sim ou não**.
Se a pergunta não distingue dois mundos, não é uma pergunta de wall — é um desejo.

| ✅ pergunta de wall | ❌ desejo disfarçado |
|---|---|
| *"Porque é que `SHADERSRC` tem `N=0` depois do walk natural do typemap pós-`R_PermA`?"* | *"Fazer os shaders funcionarem"* |
| *"Porque é que `func_0041F700` executa 27 539 083 iterações (99,96% do rasto) no boot?"* | *"Optimizar o walk de tipos"* |
| *"Porque é que `func_002545B0` lê `arg+0x7C` como sentinela de lista quando esse offset é `matriz[0][3]`?"* | *"Corrigir a parede 1"* |
| *"Porque é que `base[0]` em `func_002182A4` é a contagem de um cabeçalho (5) e não um ponteiro de pool?"* | *"Arranjar os pools"* |

**Escolhe a parede por custo, não por ambição.** O inventário das paredes vivas está
no `.planning/ROADMAP.md`, secção `### Phase 11` (medido 2026-08-01/02), e a wall D
(shaders) está em uma frase no `CLAUDE.md`.

**Lê o que já está ELIMINADO antes de abrir a boca.** O `ROADMAP.md` da Fase 11 tem uma
lista *"O que já está ELIMINADO por medição"* (registry populado, objecto bem construído,
lift fiel em 4 suspeitas, cadeia de indirecções sã, listas de irmãos bem formadas, walk
exterior termina). Reabrir qualquer uma dessas sem evidência nova **não demonstra o
método — demonstra que ninguém leu o ledger.**

---

## 2. Âncoras — EA, símbolo Ghidra, tag de log

Uma sessão sem âncoras vira conversa. Escreve as três, no cabeçalho do ledger:

| âncora | o que é | onde a arranjas |
|---|---|---|
| **EA guest** | o endereço no espaço do PS3, ex. `0x0041F700` | log do boot, `[STATE]`, `[ICALL]`, `nm`/`objdump` do lift, tabela do ROADMAP |
| **símbolo Ghidra** | o nome/fronteira que o Ghidra inferiu, ex. `.opd.FUN_0041f700  size=296` | `tools/ghidra_lookup.py <ghidra_out> 0xEA` |
| **tag de log** | o prefixo que a instrumentação emite, ex. `[LDRSH]`, `[INTROSEQ]`, `[MOVIEFSM]`, `[STATE]`, `[PS3T]` | `grep -rn '\[TAG\]' runtime/ libs/ games/gow2/hooks/` |

**EA → código original**, o passo que muda o jogo:

```bash
python3 tools/ghidra_lookup.py ../gow2-recomp/ghidra_out 0x0041F700
```

**EA → função liftada** (o lift é gitignored; vive no checkout de build):

```bash
grep -ln "^void func_0041F700" ../gow2-recomp/recomp_macos_v2/*.cpp
awk '/^void func_0041F700/,/^}/' ../gow2-recomp/recomp_macos_v2/ppu_recomp_001.cpp
```

**Compara sempre os dois.** É essa comparação que separa "o jogo é assim" de "o lifter
partiu isto" — e as duas conclusões levam a fixes opostos.

### Convenções do guest que já custaram conclusões erradas

- **`obj` vs `obj+4`.** Vários caminhos recebem o objecto **deslocado 4 bytes**
  (`param_2 - 4` logo à entrada). Por isso o mesmo campo aparece como `+0x7C` numa
  função e `+0x80` noutra. Antes de dizer "offsets diferentes", confirma a base.
- **Duas famílias de objectos, distinguidas pelo `w0` (a palavra em `obj+4`)** — tratá-las
  como uma foi a origem de várias conclusões erradas:

  | família | `w0` | `+0x7C` é |
  |---|---|---|
  | listas | `0xC0010001` | lista intrusiva correcta (auto-ligada quando vazia) |
  | registos WAD | `0x40030001` | `matriz[0][3]` |

- **Big-endian.** Um `*(ushort *)(obj + 6)` no C do Ghidra são os **16 bits BAIXOS** da
  palavra de 32 bits em `obj+4`. Não é um campo novo.

---

## 3. Ferramentas — copiar e colar

### 3.1 O oráculo estático (Ghidra)

O `ghidra_out/` já existe no checkout de build (`../gow2-recomp/ghidra_out`, 5 JSONs,
13 143 funções, 13 096 com C decompilado). **Não o regeneres por hábito**: a
auto-análise com decompilação demora dezenas de minutos.

```bash
# regenerar SÓ se estiver stale (custo: dezenas de minutos)
cd ../gow2-recomp && ./analyze_eboot_ghidra.sh              # ou games/gow2/analyze_eboot_ghidra.sh

# de um EA para o C decompilado do jogo ORIGINAL
python3 tools/ghidra_lookup.py ../gow2-recomp/ghidra_out 0x002545B0

# quem menciona este EA / nome (chamadas directas; indirectas não aparecem)
python3 tools/ghidra_lookup.py ../gow2-recomp/ghidra_out 0x0041F700 --callers --no-c

# procurar um padrão no C decompilado de todas as funções
python3 tools/ghidra_lookup.py ../gow2-recomp/ghidra_out --grep 'SHADER' --max 40
python3 tools/ghidra_lookup.py ../gow2-recomp/ghidra_out --grep 'PTR_DAT_0053ef1c' --max 40

# quem escreve um offset de struct
python3 tools/ghidra_lookup.py ../gow2-recomp/ghidra_out --writes 0xF4
```

**Limitações medidas do `--writes` e do `--callers`** (não são bugs teus, são do
decompilador — conta com elas):

- `--writes 0xC8` devolve **zero** para um campo que é escrito, porque o Ghidra emite o
  offset em **decimal** (`param_1 + 200`) quando o tipo inferido é `char *`. Quando o
  `--writes` der vazio, tenta também `--grep 'param_1 \+ 200'` com o valor decimal.
- `--callers` procura o nome/endereço **no C decompilado**. Uma função só alcançada por
  **vtable** ou **tabela de tipos** dá `(nenhum -- pode ser chamada indirecta / por
  tabela)`. Isso é informação, não falha: significa *"procura quem escreve a tabela"*.
- O nome `.opd.FUN_XXXXXXXX` é a âncora pelo **descritor OPD**; o EA e o `size` batem
  com o `func_XXXXXXXX` do lift, mas o `size` do Ghidra pode **abranger fragmentos** que
  o lifter partiu em funções separadas (ver §3.3).

#### MCP de Ghidra — opcional, e nunca o oráculo

O MCP (navegação ao vivo por um Ghidra aberto) é **opcional**: o caminho determinista é o
`ghidra_out/` estático com `MANIFEST.json` + `ghidra_lookup.py`, e é esse que o
`VERIFY_ORACLE=1` lê. **Política: leitura e anotação apenas** — o MCP pode decompilar, pedir
xrefs e renomear/comentar **dentro da base de dados do Ghidra**, e não pode exportar ou
reescrever o programa, aplicar patches ao binário, nem produzir código de produto
(`runtime/`, `libs/`, hooks, `ppu_recomp_*.cpp` — anti-padrão 8 desta página). Setup, versão
fixada, whitelist de tools e o procedimento de paridade:
[`docs/GHIDRA_MCP_SETUP.md`](GHIDRA_MCP_SETUP.md).

### 3.2 Portões do lift

```bash
LIFT=../gow2-recomp/recomp_macos_v2
./games/gow2/verify_lift.sh "$LIFT"                 # delta contra baselines congelados
VERIFY_ORACLE=1 ./games/gow2/verify_lift.sh "$LIFT" # passo de oráculo, opt-in (RE-01/Fase 20)
./games/gow2/apply_all_patches.sh                   # gate de delta dos patch_*.py
```

O default **não muda** com `VERIFY_ORACLE`: sem a variável, o `verify_lift.sh` corre
exactamente como antes e com o mesmo `rc`. É de propósito — está no caminho de aceitação
(`accept_relift.sh`).

### 3.3 Auditoria de fronteiras / fallthrough

O bug que assombrou o projecto meses foi um **fallthrough cross-fragment** errado
(`func_002550C8` caía em `func_002550E8` em vez de `func_00255178`). O padrão a auditar:

```bash
grep -n "g_trampoline_fn = (void(\*)(void\*))func_" ../gow2-recomp/recomp_macos_v2/*.cpp
```

Para cada ocorrência, compara o **EA do alvo** com o **EA do site**. Alvo com EA **menor**
que o site = fallthrough para trás disfarçado = suspeito. Alvo com EA maior e contíguo é
o caso normal de fragmentação.

### 3.4 Smoke in-boot (só quando a evidência estática não chega)

```bash
cd ../gow2-recomp
(
  set -a; . ./env_gow2.sh; set +a
  unset $(env | awk -F= '/^PS3_TRACE_/ {print $1}') 2>/dev/null || true
  export PS3_NO_RSX=1 PS3_RSX_BACKEND=trace
  exec ./boot_gow2 EBOOT.ELF
) > /tmp/re_sessao.log 2>&1 & BPID=$!
sleep 25
kill -TERM "$BPID" 2>/dev/null; sleep 1; kill -9 "$BPID" 2>/dev/null
grep -cE '\[MOVIEFSM\] st620|\[STATE\]' /tmp/re_sessao.log
```

**Regras não negociáveis do smoke:**

- `PS3_NO_RSX=1` e `PS3_RSX_BACKEND=trace` quando não estás a medir pixels.
- **Mata por PID** (`kill -TERM $PID`, depois `-9`). **NUNCA `pkill -f boot_gow2`** —
  já pôs esta máquina em load 75 a matar processos de outras sessões.
- Logs **só em `/tmp`**. Para o git vão contagens e excertos curtos, nunca o log.
- Probes novas **gated por env var, OFF por default** — no-off o baseline não muda.

### 3.5 Quando o fix graduar

Um achado vira código por esta ordem, e não por outra:

1. **`patch_*.py` idempotente** no `../gow2-recomp/recomp_mid_v2/` — sobrevive a re-lift.
2. **`[[midasm_hook]]`** no `games/gow2/config/gow2_recomp.toml` + corpo em
   `games/gow2/hooks/gow2_midasm_hooks.cpp` — quando o fix é *ao lado* de uma instrução.
3. **`[[functions_override]]` (weak)** + `games/gow2/hooks/gow2_func_overrides.cpp` —
   quando o fix **envolve** o corpo da função.
4. **Contrato executável** em `CONTRACTS.tsv` — sem isto o fix não tem aceite.

---

## 4. Hipótese → experimento **discriminador**

Uma hipótese sem experimento discriminador é uma opinião. O experimento tem de ter
**sinais previstos diferentes** conforme a hipótese seja verdadeira ou falsa — se
previres o mesmo nos dois casos, não mede nada.

Escreve a tabela **antes** de correr:

| # | Hipótese | Experimento discriminador | Sinal previsto se VERDADE | Sinal previsto se FALSO |
|---|---|---|---|---|
| H1 | o lifter perdeu o limite do walk | comparar o C do Ghidra em `0xEA` com a função liftada, instrução a instrução nas máscaras | o original tem um visited-set/contador que o lift não tem | as máscaras batem bit a bit; a forma exponencial está no jogo |
| H2 | a recursão é auto-recursão em `0xEA` | procurar chamada a `0xEA` dentro de `0xEA`, no original e no lift | há `bl` / chamada directa a si própria | só há `ps3_indirect_call` por vtable — o limite tem de ir para outro lado |

**Boas práticas que já pouparam dias:**

- **Uma variável que parte o mundo ao meio.** O `PS3_COMMIT_HIGH` refutou "falta de
  commit" numa única corrida.
- **Sonda de CONTROLO em cada bissecção.** Sem uma sonda que *tem* de aparecer, um
  silêncio não distingue "não passou aqui" de "a sonda não foi compilada". Foi assim
  que se descobriu que o critério das Fases 7–10 media uma **string inexistente**.
- **Suspeita de nós primeiro.** Auditar os nossos paliativos/breakers é barato e já
  quase foi a causa (o `0x47D00000` da Fase 10 era a zona pinada do paliativo TYPE15,
  não comportamento do jogo).
- **Timebox ~2h por rodada.** Diagnóstico rigoroso é entrega válida.

---

## 5. Aceite medido — e o rótulo do nível de evidência

Toda a afirmação leva um **nível de evidência**. Sem rótulo, a afirmação não conta.

| nível | significa | exemplo |
|---|---|---|
| **in-boot** | observado num boot real, com o comando e a contagem escritos | `grep -c '\[INTROSEQ\] CE03C' /tmp/x.log` → `7` |
| **offline-unit** | provado fora do boot: estático, link, contrato, unit | `nm -m` dá weak no lift e external no binário; máscaras do lift == C do Ghidra |
| **não-exercitado** | o caminho existe mas **não** foi percorrido | o ramo de abort do CE03C nunca disparou no smoke de 25 s |

O que conta como evidência, por ordem de preferência:

1. **Contagem de log com o comando escrito** (`grep -c ... /tmp/log` → `N`), não "vi passar".
2. **Contrato executável** do `CONTRACTS.tsv` a verde/vermelho conforme previsto.
3. **Prova por mutação**: injecta o defeito, o gate tem de ficar `rc=1`; reverte, `rc=0`.
   Sem esta prova, um gate verde é decorativo.
4. **Diff estático binário↔lift** (Ghidra vs `func_*` liftada), instrução ou máscara.
5. **Hash/PNG/`nm`/`objdump`** para artefactos.

**Nunca**:

- afirmar "funciona"/"renderiza" sem observação — distingue sempre **provado in-boot**
  de **offline/unit** de **não-exercitado**;
- reclamar **pixels de jogo** por trabalho de tooling/lift — overlay e demo não são
  conteúdo do jogo;
- copiar um número de um plano antigo em vez de o medir outra vez.

---

## 6. Anti-padrões — nomeados, e proibidos

Estes são **proibidos como "fix"** (regra 5 do `CLAUDE.md`). No máximo são ferramentas de
diagnóstico, sempre **gated por env var, OFF por default e declaradas** no ledger:

1. **CRC bypass** — desligar a verificação para o conteúdo "passar".
2. **Stub de combination** — devolver um objecto vazio no sítio onde o jogo espera a
   combinação real.
3. **Estampar magic à mão** — escrever o valor mágico que o jogo devia ter produzido.
4. **Null-check cosmético** — `if (!p) return;` sobre o sintoma, sem saber porque é null.
5. **Mascarar um guard** — silenciar o guard que estava a dizer a verdade.
6. **`PS3_SHADER_DEMO` como aceite de conteúdo** — o shader de demo não é conteúdo do jogo.
7. **Gate de diagnóstico a fechar uma parede** — `PS3_LIST254_EMPTY_IF_NULL`,
   `PS3_POOL_NULL_IF_BAD`, `PS3_LIST547_EMPTY_IF_BAD` servem **só para comparação**.
   Ficam OFF. **Um gate NUNCA fecha uma parede.**
8. **LLM a reescrever um chunk liftado** — nunca gerar nem "corrigir" um
   `ppu_recomp_*.cpp` com um modelo. O lift é saída determinista do
   `tools/ppu_lifter.py`; um fix nele vive num `patch_*.py` idempotente, num
   `[[midasm_hook]]` ou num weak override — **nunca** em texto produzido por IA. A IA é
   **navegadora do mapa guest**, com oráculos determinísticos por trás; não é emissora
   de lift.
9. **Forjar o aceite / o verde** — escrever "fechado" sem a medição que o suporta.
   Um veredicto **"ainda aberto" com duas refutações medidas é uma entrega**; um fecho
   forjado é dano permanente ao ledger.
10. **`pkill -f boot_gow2`** — mata processos de outras sessões e deixa órfãos. Mata por PID.
11. **`git add -A` / `git add .`** — o repo tem 20 GB de dados de jogo não-versionáveis
    ao lado. Paths explícitos, sempre.
12. **Commitar dados de jogo ou logs** — `EBOOT.*`, `*.psarc`, `*.self`, `extracted/`,
    `movie_cache/`, `*.m2v`, `*.wad_ps3`, os `ppu_recomp_*.cpp` liftados, os JSON do
    `ghidra_out/` e os logs de boot. Para o git vão **contagens e excertos**.
13. **"Corrigir" divergindo do CELL** — o fix é fiel ao comportamento do PPC original,
    mesmo quando o comportamento original parece errado.

---

## 7. O ledger da sessão (obrigatório)

Uma sessão termina numa página em `docs/re_sessions/YYYY-MM-DD-wall-<slug>.md`:

```markdown
# Sessão de RE — <wall> — YYYY-MM-DD

## Parede escolhida e porquê (custo, não ambição)
## Pergunta (uma frase)
## Âncoras (EA / símbolo Ghidra / tag de log)
## Ferramentas usadas (comando + rc + o que devolveu)
## Hipóteses
| # | hipótese | experimento discriminador | resultado | nível de evidência |
## O que fica REFUTADO por medição
## Veredicto: aberto / avançado / resolvido
## O que esta sessão NÃO afirma
## Próximo experimento para quem retomar
```

As secções **"O que esta sessão NÃO afirma"** e **"Próximo experimento"** não são
opcionais: a primeira é o que impede o ledger de inflacionar com o tempo; a segunda é o
que faz a sessão seguinte começar onde esta acabou em vez de recomeçar do zero.

Achados que ficam por fechar vão também para `.planning/WINDOWS.md`
(`kind` ∈ `stub | unmet-truth | unrun-verify | deviation | …`), que é o registo que
bloqueia o `/gsd-ship` enquanto houver janelas abertas.
