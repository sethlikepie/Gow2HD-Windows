#!/usr/bin/env python3
"""Gera o PATCH_MIGRATION.tsv -- ledger de migracao patch->mecanismo.

Fase 16, Plano 16-01, Task 2 (requisito LEDG-01).

Irmao de `gen_catalog.py`, nao extensao dele: o catalogo e' 100% derivado e
"NUNCA editado a mao"; o ledger tem 4 colunas que SAO editadas por humano
(`alvo`, `prioridade`, `aceite`, `estado`). Misturar os dois no mesmo gerador
apagaria a fronteira derivado/editado que a 16-CONTEXT.md fixou -- e' essa
fronteira que impede o ledger de apodrecer no primeiro regen.

Universo: os `patch_*.py` de `../gow2-recomp/recomp_mid_v2` (o corpus que o
`build_macos.sh`/`apply_all_patches.sh` de facto aplicam). O espelho subtree
`games/gow2/recomp_mid_v2` esta' atrasado e serve APENAS para a coluna
`so_no_build` e para a nota de divergencia no cabecalho; os patches que so'
existem nele NAO ganham linha (nao fazem parte do que o build corre).
Reconciliar o subtree e' divida declarada, fora do escopo desta fase.

Colunas obrigatorias (LEDG-01 / ROADMAP):
  patch, classe, alvo, ea, prioridade, aceite, estado
Colunas auxiliares derivadas:
  subclasse_catalogo, so_no_build, marcador

Derivacao da coluna `classe` (mapa de 16-RESEARCH.md; NUNCA copiada a mao --
sai de um join com PATCH_CATALOG.tsv pelo nome do ficheiro):

  PATCH_CATALOG.classe | PATCH_CATALOG.subclasse | PATCH_MIGRATION.classe
  ---------------------+-------------------------+-----------------------
  PROBE                | orfao-sem-fonte         | OBSOLETE
  PROBE                | qualquer outra          | PROBE
  FUNCIONAL            | opd                     | OPD
  FUNCIONAL            | qualquer outra          | CORRECTNESS

`BUILD` pertence ao dominio de LEDG-01 mas hoje nao e' produzido: nenhum
patch do corpus mexe so' em build/scripts. Fica reservado, nao forcado.

Heuristica da coluna `ea` (declarada como heuristica no cabecalho do TSV --
nao e' uma medicao, e' um palpite verificavel pelo humano na 16-02), na
ordem, o primeiro que casar decide:

  1. `func_XXXXXXXX` no NOME do ficheiro
  2. `0xXXXXXXXX` no NOME do ficheiro
  3. token hexadecimal no NOME (ex.: `2550c8` em patch_fallthrough_2550c8.py)
     CUJO `func_<pad8>` aparece de facto no CORPO -- a correlacao nome<->corpo
     e' o que impede um token como `d3d12` de virar uma EA inventada
  4. primeiro `void func_XXXXXXXX` no CORPO (a assinatura que o patch ancora)
  5. `func_XXXXXXXX` numa linha `SIG =` / `FUNC_SIG` do CORPO
  6. `-` (nenhuma EA identificavel -- e' informacao, nao falha: um patch sem
     EA nao e' migravel para midasm/weak e por isso nao pode ser P0)

`--merge <ledger>` preserva as 4 colunas humanas das linhas cujo patch ainda
existe; as colunas derivadas sao sempre recalculadas (um regen depois de um
re-lift tem de reflectir o catalogo novo, nao o congelado).

Uso:
  .venv/bin/python3 games/gow2/lift_baseline/gen_migration_ledger.py \\
    [--catalog games/gow2/lift_baseline/PATCH_CATALOG.tsv] \\
    [--build-dir ../gow2-recomp/recomp_mid_v2] \\
    [--mono-dir games/gow2/recomp_mid_v2] \\
    [--merge games/gow2/lift_baseline/PATCH_MIGRATION.tsv] \\
    [-o games/gow2/lift_baseline/PATCH_MIGRATION.tsv]

Sem `-o` escreve em stdout. Paths relativos resolvem contra a raiz do
monorepo (deduzida da localizacao deste ficheiro), nao contra o cwd.
"""
from __future__ import annotations

import argparse
import datetime
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
MONOREPO_ROOT = HERE.parents[2]

COLUMNS = (
    "patch",
    "classe",
    "alvo",
    "ea",
    "prioridade",
    "aceite",
    "estado",
    "ea_pista_nome",
    "subclasse_catalogo",
    "so_no_build",
    "marcador",
)

# As unicas colunas que um humano edita (16-CONTEXT.md). `--merge` preserva
# estas e so' estas.
HUMAN_COLUMNS = ("alvo", "prioridade", "aceite", "estado")

CLASSES_LEDGER = ("CORRECTNESS", "PROBE", "OPD", "BUILD", "OBSOLETE")

# --------------------------------------------------------------------------
# Baseline de RUNTIME (plano 16-03).
#
# Ao contrario das contagens e da classificacao, estes numeros NAO sao
# derivaveis: saem de correr o verify_lift.sh e um boot, uma vez cada. Por
# isso vivem aqui como constante datada -- se ficassem como linha `#` escrita
# a mao no TSV, o primeiro `--merge` apagava-os (render_tsv reconstroi o
# cabecalho inteiro).
#
# REGRA (G5 / CLAUDE.md regra 4): so' se escreve aqui o que foi OBSERVADO no
# log da corrida. Um campo que nao foi exercitado fica a None e o cabecalho
# di-lo com todas as letras -- nunca se preenche por inferencia. Pondo
# BASELINE_VERIFY/BASELINE_SMOKE a None, o cabecalho volta sozinho ao estado
# "POR MEDIR ... [nivel de evidencia: nao-exercitado]".
# --------------------------------------------------------------------------
BASELINE_VERIFY: dict | None = {
    "data": "2026-08-02",
    "lift": "../gow2-recomp/recomp_macos_v2",
    "lift_sha256": "73c8abd21abb17e829fc4186b55a5b2395a3d0e4b4f8019bf6bd8e8cc901ae7f",
    "cmd": "PS3_ENGINE_ROOT=$(pwd) ./games/gow2/verify_lift.sh ../gow2-recomp/recomp_macos_v2",
    "rc": 0,
    "passos": "lift_parity=PASS MANIFEST=PASS audit_boundaries=PASS",
    "deltas": (
        "lift_parity current=77 baseline=77 novos=0 resolvidos=0"
        " | MANIFEST_DEBT current=0 baseline=36 novos=0 resolvidos=36"
        " | audit_boundaries current=180 baseline=180 novos=0 resolvidos=0"
    ),
    "log": "/tmp/m0_fase16_verify_lift.log (FORA dos repos, G6 -- nao versionado)",
    "notas": (
        "verde a' primeira, sem re-corrida e sem VERIFY_ORACLE (isso e' Fase 20)."
        " A divida MANIFEST congelada (36) aparece toda como resolvida neste lift:"
        " o gate passa por delta, nao por limiar"
    ),
}

BASELINE_SMOKE: dict | None = {
    "data": "2026-08-02",
    "bin": "../gow2-recomp/boot_gow2 (mtime 2026-07-29 15:07; NAO reconstruido nesta corrida)",
    "lift": (
        "recomp_macos_v2 (producao) -- RESSALVA MEDIDA: 5 dos 7 ppu_recomp_*.cpp"
        " sao MAIS RECENTES que o binario (31/jul vs 29/jul), logo este smoke NAO"
        " exercita o lift que o verify_lift acima verificou"
    ),
    "recipe": (
        "env_gow2.sh + PS3_NO_RSX=1 PS3_RSX_BACKEND=trace PS3_PERF_FSM=1"
        " PS3_MOVIE_EOS=0; PS3_TRACE_* limpos; probes OFF"
    ),
    "duration_s": 25,
    "kill": "PID (TERM, depois -9) -- nunca pkill -f; orfaos pos-run = 0",
    "st620_max": (
        "11  (sequencia observada 0->1->3->3->11->11, overlay_done=1; o sentinela"
        " 4294967295 = 0xFFFFFFFF fica FORA do max, por smoke_m0_baseline.sh)"
    ),
    "sticky_hits": (
        "NAO-OBSERVAVEL -- ps3_fios_sticky_publish/peek/consume nao imprimem nada"
        " (zero fprintf com 'sticky' em runtime/ e libs/), por isso um grep daria"
        " sempre 0 e 0 NAO significa 'sticky morto'. Metrica sem emissor, nao"
        " metrica a zero"
    ),
    "oob_or_ffff_hits": (
        "0  (ausencia REAL: zero ocorrencias de 'OOB' ou '0xFFFF' em 3707 linhas;"
        " o log termina em [CONTENT] boot logo queue DONE, sem crash)"
    ),
    "bytes_read": (
        "NAO-OBSERVADO -- os 3 emissores (trace dump, close stats, GATE-FORCE"
        " R_PermA full) nao dispararam em 25s com MOVIE_EOS=0: o unico"
        " [movieio] open foi o gow2.psarc, o R_PermA nao chegou a ser lido."
        " Logo o aceite smoke:bytes_read=20169344 de patch_fallthrough_2550c8.py"
        " CONTINUA nao-exercitado"
    ),
    "log": "/tmp/m0_fase16_smoke.log (231805 bytes, FORA dos repos, G6 -- nao versionado)",
    "notas": (
        "UMA corrida, sem retry (nao houve falha de vm commit). 52068 lifted"
        " functions. st620_max=11 esta' acima do gate >=3 do CLAUDE.md."
        " Para a Fase 21 comparar como deve ser, o smoke tera' de ser refeito"
        " sobre um binario reconstruido do lift corrente"
    ),
}

# --------------------------------------------------------------------------
# Notas sobre o dominio da coluna `alvo` (renderizadas a seguir a' linha
# "# dominio:"). Vivem aqui, e nao como linha `#` escrita a mao no TSV, porque
# render_tsv reconstroi o cabecalho inteiro a cada `--merge` -- uma nota escrita
# no ficheiro desaparecia no primeiro regen.
#
# `lifter` foi acrescentado ao dominio na Fase 17 (plano 17-03). Antes disso o
# unico destino disponivel para um defeito de TRADUCAO era `midasm` ou
# `runtime`, e foi por falta de vocabulario que o patch_fallthrough_2550c8.py
# ficou classificado como `midasm` na Fase 16.
# --------------------------------------------------------------------------
NOTAS_ALVO: list[str] = [
    "# alvo=lifter (dominio alargado na Fase 17 / plano 17-03): o defeito e' de"
    " TRADUCAO e o fix pertence ao tools/ppu_lifter.py -- nem hook host, nem"
    " runtime, nem tabela declarada no TOML. Envolve-lo num mid-asm esconderia"
    " um bug do lifter atras de codigo host, e o proprio plano de origem manda"
    " o contrario: 'preferir fix no lifter se sistematico'.",
    "#   patch_fallthrough_2550c8.py: RECLASSIFICADO de alvo=midasm para"
    " alvo=lifter nesta fase. A causa medida e' um fallthrough cross-fragment"
    " ERRADO -- func_002550C8 caia em func_002550E8 em vez de func_00255178.",
    "#   Continua estado=todo e P0: a reclassificacao corrige o MECANISMO, nao"
    " declara o trabalho feito. A auditoria sistematica do padrao (alvo com EA"
    " guest MENOR que o site) esta' na lista P1 do CLAUDE.md e nao e' da Fase 17.",
    "# estado=redundant (dominio alargado na Fase 18 / plano 18-03): o mecanismo"
    " ja' produz o aceite SEM o patch e sem qualquer declaracao -- logo nao houve"
    " migracao nenhuma, descobriu-se que o patch nao era preciso. E' distinto de"
    " `migrated` (o patch era necessario e o mecanismo passou a fazer o trabalho)"
    " e de `wont` (decidiu-se nao migrar). A distincao existe para a metrica: um"
    " `redundant` NAO conta para os >=3 fixes re-lift-safe do XEN-05; contam-se em"
    " separado e a Fase 21 mede os dois numeros.",
]

# --------------------------------------------------------------------------
# Notas de classificacao por familia. Mesma razao de viverem aqui: o cabecalho
# e' reconstruido a cada `--merge`.
# --------------------------------------------------------------------------
NOTAS_ESTADO: list[str] = [
    "# subclasse=jumptable (CLASSIFICADA na Fase 18 / plano 18-03, por medicao"
    " numa escada A/B/C sobre dois re-lifts completos da MESMA EBOOT):",
    "#   (A) re-lift SEM patch e SEM --config: os dois aceites grep do ledger ja'"
    " saem emitidos, e nao por acaso -- o `case` cai dentro do switch da funcao"
    " certa (func_002A209C e func_002B11B8) com 54 e 22 alvos distintos, os MESMOS"
    " numeros que os proprios patches instalariam (54/54 e 22/22, oraculo do 18-02),"
    " e com `default: ps3_indirect_call(ctx); return;`. O discover_jump_tables"
    " encontra as duas tabelas sozinho => estado=redundant nos dois.",
    "#   (B) re-lift SEM patch e COM --config (as duas tabelas declaradas em"
    " games/gow2/config/gow2_switch_tables.toml): identico ao (A), 54 e 22. A"
    " declaracao nao e' load-bearing hoje; e' rede. Como (A) ja' passa, a regra do"
    " 18-CONTEXT manda `redundant` e NAO `migrated` -- inflar aqui seria contar"
    " como fix re-lift-safe um patch que nunca foi preciso.",
    "#   (C) correr o patch por cima do lift (B): patch_jumptable_2a209c.py"
    " verifica os 54 alvos e imprime 'nada a injectar' (rc=0, SHA256 do chunk"
    " IGUAL byte a byte) -- e' ja' um verificador idempotente. Ha' RESSALVA no"
    " 2b11b8: ver a nota seguinte.",
    "#   RESSALVA MEDIDA (patch_jumptable_2b11b8.py): ao contrario do 2a209c, este"
    " NAO e' no-op no passo (C) -- ainda substitui o calculo do CTR (a leitura de"
    " TOC-0x151C na memoria guest) por uma tabela hardcoded. O switch fica intacto"
    " e nao ha' dupla injeccao, mas esse residuo NAO e' a jump table e portanto NAO"
    " esta' coberto pelo veredicto `redundant`, que se refere ao `aceite` desta"
    " linha (alvo=switch_table). Medido estaticamente na EBOOT: [TOC-0x1694] ="
    " 0x002A2134 e [TOC-0x151C] = 0x002B1228 -- exactamente as bases que os patches"
    " hardcoded usam, ou seja a leitura que o lift faz esta' correcta NA IMAGEM."
    " Isso enfraquece muito a hipotese original ('ICALL-BAD ctr=0x27182818') mas"
    " NAO a refuta em runtime. Por isso o ficheiro NAO foi tocado nem apagado: o"
    " lift de producao recomp_macos_v2 ainda leva este patch aplicado (marcador"
    " 'Task4 FIX' presente) e remover o residuo exige um A/B IN-BOOT no caminho do"
    " typemap/WAD, que o smoke de intro de 25s nao exercita.",
]

# Colunas do PATCH_CATALOG.tsv (a linha de cabecalho la' e' comentada, por
# isso lemos os nomes do proprio ficheiro quando possivel e caimos aqui).
CATALOG_COLUMNS_FALLBACK = (
    "patch",
    "escreve",
    "classe",
    "subclasse",
    "no_gate",
    "razao",
    "marcador",
    "chunk_alvo",
)

_RE_NAME_FUNC = re.compile(r"func_([0-9A-Fa-f]{8})")
_RE_NAME_HEX0X = re.compile(r"0x([0-9A-Fa-f]{8})")
_RE_NAME_TOKEN = re.compile(r"[0-9A-Fa-f]{4,8}")
_RE_BODY_VOID = re.compile(r"void\s+func_([0-9A-Fa-f]{8})")
_RE_BODY_SIG = re.compile(r"(?:FUNC_SIG|SIG\s*=)[^\n]*func_([0-9A-Fa-f]{8})")


# ---------------------------------------------------------------------------
# Derivacao
# ---------------------------------------------------------------------------


def map_classe(classe_catalogo: str, subclasse_catalogo: str) -> str:
    """Mapa catalogo -> ledger (ver docstring do modulo). Funcao pura."""
    if classe_catalogo == "PROBE":
        if subclasse_catalogo == "orfao-sem-fonte":
            return "OBSOLETE"
        return "PROBE"
    if classe_catalogo == "FUNCIONAL":
        if subclasse_catalogo == "opd":
            return "OPD"
        return "CORRECTNESS"
    raise ValueError(
        f"classe de catalogo desconhecida: {classe_catalogo!r}/"
        f"{subclasse_catalogo!r} -- o mapa tem de ser actualizado "
        "explicitamente, nunca cair num default silencioso"
    )


def default_alvo(classe_ledger: str) -> str:
    """Alvo por omissao. Vazio = decisao humana pendente (16-02)."""
    if classe_ledger == "PROBE":
        return "keep-probe"
    if classe_ledger == "OBSOLETE":
        # Proposta, nao execucao: ninguem apaga nada nesta fase.
        return "delete"
    return ""


def normalize_ea(hexstr: str) -> str:
    """`2550c8` / `002550C8` -> `0x002550C8`."""
    return "0x" + hexstr.upper().rjust(8, "0")


def detect_ea(name: str, src: str) -> str:
    """Heuristica de EA (6 ramos, ver docstring do modulo). Funcao pura."""
    m = _RE_NAME_FUNC.search(name)
    if m:
        return normalize_ea(m.group(1))

    m = _RE_NAME_HEX0X.search(name)
    if m:
        return normalize_ea(m.group(1))

    # Ramo 3: token hex do nome CORRELACIONADO com o corpo.
    stem = name[: -len(".py")] if name.endswith(".py") else name
    for token in _RE_NAME_TOKEN.findall(stem):
        if not any(c.isdigit() for c in token):
            continue
        pad = token.upper().rjust(8, "0")
        # src.upper() dos dois lados: o corpo escreve `func_002550C8` mas o
        # token do nome vem em minusculas (`2550c8`).
        if f"FUNC_{pad}" in src.upper():
            return "0x" + pad

    m = _RE_BODY_VOID.search(src)
    if m:
        return normalize_ea(m.group(1))

    m = _RE_BODY_SIG.search(src)
    if m:
        return normalize_ea(m.group(1))

    return "-"


def ea_pista_do_nome(name: str) -> str:
    """Candidato a EA lido SO' do nome do ficheiro, NAO confirmado pelo corpo.

    A convencao `patch_<hex>_<descricao>.py` sugere um endereco em quase todo
    o corpus, mas em dezenas de scripts o corpo nunca menciona essa funcao (o
    patch ancora numa fatia de codigo, num vizinho, ou o nome refere o SITIO e
    nao a funcao). Escrever esse numero na coluna `ea` seria forjar uma
    medicao; fica aqui, declarado como pista a verificar na 16-02.
    Devolve "" quando o nome nao tem nenhum token hexadecimal plausivel.
    """
    stem = name[: -len(".py")] if name.endswith(".py") else name
    for token in _RE_NAME_TOKEN.findall(stem):
        if not any(c.isdigit() for c in token):
            continue
        return "0x" + token.upper().rjust(8, "0")
    return ""


# ---------------------------------------------------------------------------
# IO
# ---------------------------------------------------------------------------


def _catalog_columns(text: str) -> tuple[str, ...]:
    """Nomes de coluna lidos da linha `# patch\\tescreve\\t...` do catalogo."""
    for line in text.splitlines():
        if line.startswith("#") and "\t" in line:
            cols = tuple(c.strip() for c in line.lstrip("#").strip().split("\t"))
            if cols and cols[0] == "patch":
                return cols
    return CATALOG_COLUMNS_FALLBACK


def load_catalog(path: Path) -> dict[str, dict]:
    """Le o PATCH_CATALOG.tsv -> {nome do patch: linha}. Read-only."""
    text = Path(path).read_text(encoding="utf-8", errors="replace")
    cols = _catalog_columns(text)
    out: dict[str, dict] = {}
    for line in text.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        campos = line.split("\t")
        # Preenche a direita: o TSV pode ter campos finais vazios aparados.
        campos += [""] * (len(cols) - len(campos))
        linha = dict(zip(cols, campos))
        out[linha["patch"]] = linha
    return out


def parse_ledger(text: str) -> dict[str, dict]:
    """Le um PATCH_MIGRATION.tsv (texto) -> {nome do patch: linha}."""
    cols: tuple[str, ...] | None = None
    out: dict[str, dict] = {}
    for line in text.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        campos = line.split("\t")
        if cols is None:
            if campos[0] == "patch":
                cols = tuple(campos)
                continue
            cols = COLUMNS
        campos += [""] * (len(cols) - len(campos))
        linha = dict(zip(cols, campos))
        out[linha["patch"]] = linha
    return out


# ---------------------------------------------------------------------------
# Construcao das linhas
# ---------------------------------------------------------------------------


def build_rows(
    catalog: dict[str, dict],
    build_dir: Path,
    mono_dir: Path,
    anterior: dict[str, dict] | None = None,
) -> list[dict]:
    """Uma linha por `patch_*.py` de `build_dir`, ordenada por nome.

    `anterior` (o ledger de uma corrida passada) so' repoe HUMAN_COLUMNS; as
    colunas derivadas sao sempre recalculadas a partir do catalogo e do disco.
    Falha ruidosamente se um patch do corpus nao estiver no catalogo -- isso
    significa catalogo desactualizado, e um silencio aqui reintroduzia
    exactamente o buraco de 50 patches que a Task 1 fechou.
    """
    anterior = anterior or {}
    mono_nomes = {p.name for p in Path(mono_dir).glob("patch_*.py")}
    rows: list[dict] = []
    em_falta: list[str] = []

    for path in sorted(Path(build_dir).glob("patch_*.py")):
        nome = path.name
        linha_cat = catalog.get(nome)
        if linha_cat is None:
            em_falta.append(nome)
            continue
        src = path.read_text(encoding="utf-8", errors="replace")
        classe = map_classe(linha_cat["classe"], linha_cat["subclasse"])
        ea = detect_ea(nome, src)
        row = {
            "patch": nome,
            "classe": classe,
            "alvo": default_alvo(classe),
            "ea": ea,
            "ea_pista_nome": ea_pista_do_nome(nome) if ea == "-" else "",
            "prioridade": "",
            "aceite": "",
            "estado": "todo",
            "subclasse_catalogo": linha_cat["subclasse"],
            "so_no_build": "0" if nome in mono_nomes else "1",
            "marcador": linha_cat.get("marcador", ""),
        }
        prev = anterior.get(nome)
        if prev:
            for col in HUMAN_COLUMNS:
                valor = prev.get(col, "")
                if valor != "":
                    row[col] = valor
        rows.append(row)

    if em_falta:
        raise SystemExit(
            f"{len(em_falta)} patch(es) do corpus de build ausentes do catalogo: "
            f"{', '.join(em_falta[:5])}{'...' if len(em_falta) > 5 else ''}\n"
            "Regenerar primeiro: gen_catalog.py ../gow2-recomp/recomp_mid_v2 "
            "games/gow2/lift_baseline/MANIFEST.tsv > "
            "games/gow2/lift_baseline/PATCH_CATALOG.tsv"
        )
    return rows


def linha_classificada(row: dict) -> bool:
    """A linha ja' passou pela decisao humana da 16-02?

    Regra (identica a' do `<verify>` do 16-02-PLAN.md, para que o numero do
    cabecalho e o do gate nunca divirjam): as 4 colunas humanas preenchidas,
    com a excepcao de `prioridade`, que e' legitimamente vazia nas linhas
    PROBE (o dominio de LEDG-01 diz "P0|P1|P2 ou vazio (so' PROBE)").
    """
    if not (row.get("alvo") and row.get("aceite") and row.get("estado")):
        return False
    return row.get("classe") == "PROBE" or bool(row.get("prioridade"))


def contar_classificacao(rows: list[dict]) -> dict[str, object]:
    """Contagens DERIVADAS das colunas humanas (nunca escritas a mao).

    O cabecalho tem de dizer quantas linhas ja' foram decididas e quais sao os
    candidatos a piloto das Fases 17/18. Se esses numeros fossem comentarios
    escritos a mao, o primeiro regen tornava-os mentira -- e' o mesmo motivo
    pelo qual `classe` e `ea` sao derivadas.
    """
    classificados = [r for r in rows if linha_classificada(r)]
    por_prio: dict[str, int] = {}
    for r in classificados:
        if r.get("prioridade"):
            por_prio[r["prioridade"]] = por_prio.get(r["prioridade"], 0) + 1
    def _p0(alvo: str) -> list[str]:
        return sorted(
            r["patch"]
            for r in classificados
            if r.get("prioridade") == "P0" and r.get("alvo") == alvo
        )
    return {
        "n_classificados": len(classificados),
        "por_prioridade": por_prio,
        "piloto_midasm": _p0("midasm"),
        "piloto_switch_table": _p0("switch_table"),
    }


def medir_contagens(
    catalog: dict[str, dict], build_dir: Path, mono_dir: Path
) -> dict[str, object]:
    """Contagens MEDIDAS no disco nesta corrida (nunca copiadas de um plano)."""
    build_nomes = {p.name for p in Path(build_dir).glob("patch_*.py")}
    mono_nomes = {p.name for p in Path(mono_dir).glob("patch_*.py")}
    return {
        "n_build": len(build_nomes),
        "n_mono": len(mono_nomes),
        "n_cat": len(catalog),
        "build_dir": str(build_dir),
        "mono_dir": str(mono_dir),
        "so_no_mono": sorted(mono_nomes - build_nomes),
    }


# ---------------------------------------------------------------------------
# Render
# ---------------------------------------------------------------------------


def linhas_baseline_runtime() -> list[str]:
    """Cabecalho do baseline medido (plano 16-03).

    Funcao pura sobre BASELINE_VERIFY/BASELINE_SMOKE. Cada bloco cai para
    "POR MEDIR ... [nivel de evidencia: nao-exercitado]" quando a constante
    e' None -- ausencia de medicao nunca vira campo com ar de preenchido.
    """
    out: list[str] = []

    if BASELINE_VERIFY is None:
        out.append(
            "# baseline verify_lift: POR MEDIR (plano 16-03) -- rc=?"
            " [nivel de evidencia: nao-exercitado]"
        )
    else:
        b = BASELINE_VERIFY
        out += [
            f"# baseline verify_lift ({b['data']}, UMA corrida, medido"
            " [nivel de evidencia: offline-unit]):",
            f"#   LIFT={b['lift']} (producao: default de build_macos.sh e"
            " apply_all_patches.sh)",
            f"#   LIFT_SHA256={b['lift_sha256']}  (cat ppu_recomp_*.cpp)",
            f"#   cmd={b['cmd']}",
            f"#   rc={b['rc']}  ({b['passos']})",
            f"#   deltas={b['deltas']}",
            f"#   log={b['log']}",
            f"#   notas={b['notas']}",
        ]

    if BASELINE_SMOKE is None:
        out.append(
            "# baseline smoke intro: POR MEDIR (plano 16-03) -- st620_max=?"
            " sticky=? oob_pos_tls=? [nivel de evidencia: nao-exercitado]"
        )
    else:
        s = BASELINE_SMOKE
        out += [
            f"# baseline smoke intro ({s['data']}, UMA corrida, medido"
            " [nivel de evidencia: in-boot]):",
            f"#   bin={s['bin']}  LIFT={s['lift']}",
            f"#   recipe={s['recipe']}  duration_s={s['duration_s']}  kill={s['kill']}",
            f"#   st620_max={s['st620_max']}",
            f"#   sticky_hits={s['sticky_hits']}",
            f"#   oob_or_ffff_hits={s['oob_or_ffff_hits']}",
            f"#   bytes_read={s['bytes_read']}",
            f"#   log={s['log']}",
            f"#   notas={s['notas']}",
        ]

    return out


def render_tsv(rows: list[dict], contagens: dict, data: str | None = None) -> str:
    data = data or datetime.date.today().isoformat()
    n_build = int(contagens["n_build"])
    n_mono = int(contagens["n_mono"])
    n_cat = int(contagens["n_cat"])
    so_no_mono = list(contagens.get("so_no_mono") or [])

    por_classe: dict[str, int] = {}
    for r in rows:
        por_classe[r["classe"]] = por_classe.get(r["classe"], 0) + 1
    resumo = " / ".join(
        f"{c}={por_classe[c]}" for c in CLASSES_LEDGER if c in por_classe
    )
    com_ea = sum(1 for r in rows if r["ea"] != "-")
    com_pista = sum(1 for r in rows if r.get("ea_pista_nome"))

    header = [
        "# PATCH_MIGRATION.tsv -- ledger de migracao patch->mecanismo (Fase 16 / LEDG-01)",
        "# regenerar esqueleto: .venv/bin/python3 games/gow2/lift_baseline/gen_migration_ledger.py"
        " --merge games/gow2/lift_baseline/PATCH_MIGRATION.tsv"
        " -o games/gow2/lift_baseline/PATCH_MIGRATION.tsv",
        "# COLUNAS HUMANAS (nao sobrescrever no regen se --merge): alvo, prioridade, aceite, estado",
        "# COLUNAS DERIVADAS (recalculadas sempre): patch, classe, ea (heuristica,"
        " so' quando CONFIRMADA pelo corpo do script), ea_pista_nome (candidato lido"
        " do nome e NAO confirmado -- verificar antes de usar), subclasse_catalogo,"
        " so_no_build, marcador",
        f"# baseline contagens ({data}, medidas nesta corrida):",
        f"#   patch_*.py monorepo ({contagens['mono_dir']}) = {n_mono}",
        f"#   patch_*.py build    ({contagens['build_dir']}) = {n_build}",
        f"#   PATCH_CATALOG linhas de dados = {n_cat}",
        f"#   linhas de dados deste ledger = {len(rows)} ({resumo})",
        f"#   EA CONFIRMADA pelo corpo do script = {com_ea}/{len(rows)}; destas"
        f" restantes, {com_pista} trazem pista nao confirmada em ea_pista_nome"
        " (sem EA verificada nao ha' midasm/weak, logo nao ha' P0)",
        f"#   divergencia build-monorepo = {n_build} - {n_mono} = {n_build - n_mono}"
        + (
            " (declarada; reconciliar o subtree e' divida do v1.0, FORA de escopo)"
            if n_build != n_mono
            else " (espelho subtree em dia nesta corrida -- a divida do v1.0 nao"
            " aparece nesta medicao; nao e' uma afirmacao sobre o conteudo dos"
            " ficheiros, so' sobre a contagem)"
        ),
    ]
    if so_no_mono:
        header.append(
            f"#   {len(so_no_mono)} patch(es) existem SO' no espelho monorepo e NAO tem"
            f" linha (o build nao os aplica): {', '.join(so_no_mono)}"
        )
    header += linhas_baseline_runtime()
    header += [
        "# dominio: classe in {CORRECTNESS,PROBE,OPD,BUILD,OBSOLETE} (BUILD reservado,"
        " hoje sem ocorrencias) | alvo in {midasm,weak,switch_table,invalid_insn,lifter,"
        "runtime,keep-probe,delete} | prioridade in {P0,P1,P2} | estado in {todo,migrated,"
        "redundant,wont}",
    ] + NOTAS_ALVO + NOTAS_ESTADO + [
        "# mapa classe (derivado, ver 16-RESEARCH.md e o docstring de gen_migration_ledger.py):",
        "#   PROBE+orfao-sem-fonte->OBSOLETE | PROBE->PROBE | FUNCIONAL+opd->OPD |"
        " FUNCIONAL->CORRECTNESS",
        "# alvo/prioridade/aceite vazios = decisao humana pendente (plano 16-02).",
        "# aceite tem de ser executavel: grep:<padrao> | contract:<id CONTRACTS.tsv> |"
        " smoke:<metrica>. 'verificar manualmente' e' proibido.",
    ]

    cls = contar_classificacao(rows)
    prio = cls["por_prioridade"]
    resumo_prio = " ".join(
        f"N_{p}={prio[p]}" for p in ("P0", "P1", "P2") if p in prio
    ) or "N_P0=0"
    midasm = cls["piloto_midasm"] or ["(nenhum P0 com alvo=midasm ainda)"]
    switch = cls["piloto_switch_table"] or ["(nenhum P0 com alvo=switch_table ainda)"]
    header += [
        f"# classificacao humana ({data}, DERIVADA das 4 colunas humanas, recontada"
        f" a cada regen): N_classificados={cls['n_classificados']} {resumo_prio}",
        "# criterio das linhas classificadas (plano 16-02, decidido a mao):",
        "#   (a) os candidatos de maior impacto nomeados pelo ROADMAP (fallthrough"
        " 2550C8, jumptables 2A209C/2B11B8, CE03C, factory OPD / ICG ctor, host inflate);",
        "#   (b) toda a familia classe=OPD com EA confirmada -- e' a unica com"
        " contratos ja' escritos em CONTRACTS.tsv, logo com aceite executavel HOJE;",
        "#   (c) linhas CORRECTNESS cujo marcador tem dono unico em CONTRACTS.tsv ou"
        " cujo marcador exclusivo foi medido presente no lift de producao.",
        "# criterio P0 (os TRES, 16-CONTEXT.md): classe in {CORRECTNESS,OPD} E toca o"
        " caminho de boot medido (intro -> WAD -> typemap) E `ea` CONFIRMADA (0x...,"
        " coluna derivada). `ea='-'` limita a P1 mesmo quando o corpo do script nomeia"
        " a funcao -- escrever `ea` a mao quebraria o regen.",
        f"# piloto sugerido Fase 17 (mid-asm): {', '.join(midasm)}",
        f"# piloto sugerido Fase 18 (jtable):  {', '.join(switch)}",
    ]

    lines = list(header)
    lines.append("\t".join(COLUMNS))
    for row in rows:
        lines.append("\t".join(row[col] for col in COLUMNS))
    return "\n".join(lines) + "\n"


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------


def _resolve(p: str) -> Path:
    path = Path(p)
    return path if path.is_absolute() else (MONOREPO_ROOT / path)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="Gera o PATCH_MIGRATION.tsv (ledger de migracao, LEDG-01)."
    )
    ap.add_argument("--catalog", default="games/gow2/lift_baseline/PATCH_CATALOG.tsv")
    ap.add_argument("--build-dir", default="../gow2-recomp/recomp_mid_v2")
    ap.add_argument("--mono-dir", default="games/gow2/recomp_mid_v2")
    ap.add_argument(
        "--merge",
        default=None,
        help="ledger anterior cujas colunas humanas devem ser preservadas",
    )
    ap.add_argument("-o", "--output", default=None, help="destino (default: stdout)")
    args = ap.parse_args(argv)

    catalog_path = _resolve(args.catalog)
    build_dir = _resolve(args.build_dir)
    mono_dir = _resolve(args.mono_dir)

    if not catalog_path.is_file():
        print(f"catalogo nao encontrado: {catalog_path}", file=sys.stderr)
        return 2
    if not build_dir.is_dir():
        print(f"nao e' um directorio: {build_dir}", file=sys.stderr)
        return 2
    if not mono_dir.is_dir():
        print(f"nao e' um directorio: {mono_dir}", file=sys.stderr)
        return 2

    anterior: dict[str, dict] = {}
    if args.merge:
        merge_path = _resolve(args.merge)
        if merge_path.is_file():
            anterior = parse_ledger(
                merge_path.read_text(encoding="utf-8", errors="replace")
            )
        else:
            print(
                f"[aviso] --merge {merge_path} nao existe; a gerar de raiz",
                file=sys.stderr,
            )

    catalog = load_catalog(catalog_path)
    rows = build_rows(catalog, build_dir, mono_dir, anterior=anterior)
    contagens = medir_contagens(catalog, build_dir, mono_dir)
    # No cabecalho ficam os paths COMO FORAM PEDIDOS (relativos a' raiz do
    # monorepo), nao os absolutos da maquina de quem correu: o TSV e'
    # versionado e um /Users/<alguem>/ la' dentro e' ruido e ma' reproducao.
    contagens["build_dir"] = args.build_dir
    contagens["mono_dir"] = args.mono_dir
    texto = render_tsv(rows, contagens)

    if args.output:
        _resolve(args.output).write_text(texto, encoding="utf-8")
    else:
        sys.stdout.write(texto)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
