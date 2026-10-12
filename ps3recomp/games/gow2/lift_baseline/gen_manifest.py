#!/usr/bin/env python3
"""Gera o MANIFEST.tsv do baseline forense do lift (RDY-0, Task 1).

O lift do GoW2 e' gitignored e contem 1662 linhas de codigo host escritas a
mao que nao existem em mais lado nenhum. Este script inventaria tudo o que
tem de sobreviver a um re-lift:

  TAG    -- etiqueta [XXX] emitida por fprintf de instrumentacao/comportamento
  SYM    -- funcao host definida num preambulo injectado
  GLOBAL -- variavel global host definida num preambulo injectado

A referencia do preambulo PURO do lifter e' lift_baseline/preamble_pure.cpp
(131 linhas, byte-identico nos chunks 007..030 do lift de producao). Tudo o
que um preambulo tem a mais do que essa referencia e' injeccao nossa.

NOTA: ppu_recomp_002.cpp NAO serve de referencia -- tem 20 linhas injectadas.

Uso:
  python3 gen_manifest.py <LIFT_DIR> > MANIFEST.tsv     gerar
  python3 gen_manifest.py <LIFT_DIR> --verify FILE.tsv  verificar (rc=1 se falta)
  ... --host-sources DIR|FICHEIRO   (repetivel) fontes host a contar tambem
  ... --no-host-sources             conta so' o lift (comportamento pre-D3)
  python3 gen_manifest.py <LIFT_DIR> --preamble-counts   contagem NO PREAMBULO
        vs no ficheiro inteiro, por simbolo do preambulo puro -- e' como se
        reproduz (e se refuta) o limiar de um marcador `AMBITO:preambulo`.

A verificacao conta por marcador em TODOS os ppu_recomp_*.cpp do lift alvo,
nunca por chunk: o lifter actual produz 7 chunks contra os 31 do lift de
producao, logo qualquer gate indexado por numero de chunk parte-se sozinho.

D3 -- porque a verificacao conta TAMBEM os hooks host versionados
------------------------------------------------------------------
Desde as Fases 17/19 uma correccao pode viver em `[[midasm_hook]]` /
`[[functions_override]]` em vez de texto injectado no lift. Quando o
`patch_ce03c_introseq_block.py` migrou, os seus 8 marcadores `[INTROSEQ]`
mudaram de casa -- 5 para `games/gow2/hooks/gow2_midasm_hooks.cpp`, 3 para
`gow2_func_overrides.cpp` -- e continuam TODOS vivos. Medido a 2026-08-03
(`games/gow2/notes/2026-08-03-seis-patches-que-nao-reaplicam.md` §5): 8 de 8
contabilizados, zero perdidos. Mesmo assim o gate reportava `-8`, porque so'
olhava para os `ppu_recomp_*.cpp`.

Isso nao e' uma divida com 8 de tamanho: e' um defeito sistemico -- CADA
migracao futura para mecanismo v1.3 geraria outra divida falsa, e o v1.3 existe
precisamente para promover migracoes. Um gate que acusa sucessos ensina toda a
gente a ignora-lo.

A GERACAO do MANIFEST continua a inventariar SO' o lift (o manifesto e' o
inventario do que o re-lift destroi). So' a VERIFICACAO soma os hooks -- e
imprime a repartição lift/host, para que uma migracao apareça como migracao e
nao como um verde opaco.

Alcance: SO' marcadores de tipo `TAG`. Um `SYM`/`GLOBAL` do MANIFEST significa
"DEFINIDO no preambulo injectado do lift"; a mesma string num hook e' um call
site, e soma-la mascararia divida real. Ver o comentario em verify().
"""
import re
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
PURE = HERE / "preamble_pure.cpp"
# Codigo host VERSIONADO onde vivem as correccoes ja migradas para mecanismo.
HOST_SOURCES_DEFAULT = HERE.parent / "hooks"

TAG_RE = re.compile(r"\[[A-Z][A-Z0-9_-]{2,}\]")
PREFIX = r"(?:ps3|f2b|ty15|fact|snd|wavdrv)_[a-z0-9_]+"
SYM_RE = re.compile(r"\b(" + PREFIX + r")\s*\(")
GLOBAL_RE = re.compile(r"\b((?:g|k)_[a-z0-9_]+)\b")


def preamble_end(text: str) -> int:
    """Indice da 1a linha DEPOIS do preambulo -- ou seja, o inicio da 1a funcao.

    Desde a Fase 19 (XEN-04) uma funcao pode comecar em duas formas: a de
    sempre (`void func_X(ppu_context* ctx) {`) ou, quando declarada em
    `[[functions_override]]` com a emissao ligada, `PPC_FUNC_IMPL(func_X) {`.
    Conhecer so' a primeira dava um preambulo longo de mais sempre que a 1a
    funcao do chunk fosse uma com override -- e a fronteira e' o que separa o
    que se conta como preambulo do que se conta como corpo. Medido a
    2026-08-02: no lift desta fase o defeito NAO chegou a disparar (o
    MANIFEST_DEBT deu exactamente os mesmos 7/36 do baseline da Fase 18), o que
    o torna precisamente o tipo de bug dependente de ordem que so' aparece
    quando ja' custa caro. Corrigido antes disso."""
    for i, line in enumerate(text.splitlines()):
        if line.startswith(("void func_", "PPC_FUNC_IMPL(func_")):
            return i
    return len(text.splitlines())


GRUPO_RE = re.compile(r"GRUPO:(\S+)")
# Nota que declara o AMBITO de contagem de um marcador PREAMBLE: o limiar mede
# as ocorrencias NO PREAMBULO, nao no ficheiro inteiro. Ver o bloco AMBITO na
# docstring do modulo e test_gen_manifest_preamble_scope.py.
AMBITO_PREAMBULO_RE = re.compile(r"AMBITO:preambulo\b")


def resolve_host_sources(specs) -> list[Path]:
    """Expande DIRs/ficheiros em ficheiros de codigo host existentes.

    Um caminho que nao exista NAO estoira -- so' nao soma nada. Uma arvore sem
    hooks (checkout parcial, worktree) tem de continuar a poder correr o gate;
    o que nao pode acontecer e' contar em silencio o que nao existe.
    """
    out: list[Path] = []
    for spec in specs:
        p = Path(spec)
        if p.is_dir():
            out.extend(sorted(q for ext in ("*.cpp", "*.c", "*.mm", "*.m")
                              for q in p.glob(ext)))
        elif p.is_file():
            out.append(p)
    return out


def verify(chunks, manifest: Path, host_sources=()) -> int:
    """Conta cada marcador do manifesto em todo o lift alvo. rc=1 se faltar.

    `host_sources` (D3) sao ficheiros host VERSIONADOS onde vivem as correccoes
    ja migradas para mecanismo v1.3. Contam para o total, e a repartição
    lift/host e' impressa -- ver o cabecalho do modulo.

    Suporta uma 5a coluna opcional (`nota`), aditiva e retro-compativel --
    marcadores sem ela seguem exactamente o comportamento pre-existente:
      - `OBSOLETO: <razao>`  -- o marcador nunca falha o gate (Repudiation
        T-03-08); e' impresso numa categoria informativa separada, nunca
        em AUSENTE/A MENOS.
      - `GRUPO:<id>`         -- marcadores com o mesmo id sao verificados
        pela SOMA de found/min_count do grupo, nao individualmente
        (D-3.1c: conversoes que trocam substituicao 1-para-1 entre dois
        marcadores nao devem punir o par quando a soma se mantem).
      - `AMBITO:preambulo`   -- (2026-08-03) o marcador e' contado SO' no
        preambulo do chunk (ate' `preamble_end()`), nao no ficheiro inteiro.
        Ver o bloco AMBITO abaixo.

    AMBITO -- porque um marcador PREAMBLE pode contar so' o preambulo
    ---------------------------------------------------------------
    O tipo `PREAMBLE` declara, no proprio codigo que o gera (ver main()), que
    existe para "confirmar que o preambulo puro SOBREVIVE no chunk alvo". A
    implementacao, porem, conta o ficheiro INTEIRO -- e o limiar congelado no
    MANIFEST.tsv era, por isso, a contagem do ficheiro inteiro. Medido a
    2026-08-03 (`games/gow2/notes/2026-08-03-trampoline-fn-deficit.md` §2) para
    `g_trampoline_fn`:

        preambulo : 56     (producao)  ==  56     (lift fresco)
        corpo     : 168697 (producao)  vs  168379 (lift fresco)

    56 de 168753 (0,03 %) mediam o proposito declarado; os outros 99,97 %
    mediam a FORMA do lifter -- quantos saltos cross-fragment ele decide
    emitir como trampolim. O fix `0585636` (jump table, 2026-07-30) recuperou
    22 dispatchers sem perder um unico dos 125 existentes, e o gate acusou a
    melhoria como perda de 318 unidades. Bumpar o valor re-armaria a armadilha
    ao proximo fix de fronteiras; o que estava errado era o AMBITO.

    Alcance DELIBERADO: so' os marcadores que trazem a nota. `ps3_indirect_call`
    tem o mesmo defeito de contrato (preambulo=7, limiar 15759) mas a sua
    correccao mexe no grupo `opd-dispatch` -- e' uma decisao a parte, com prova
    a parte, e NAO foi feita aqui.
    """
    wanted: list[tuple[str, str, int, str, str]] = []
    for line in manifest.read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        parts = line.split("\t")
        marker, kind, n, chunks_s = parts[:4]
        nota = parts[4] if len(parts) > 4 else ""
        wanted.append((marker, kind, int(n), chunks_s, nota))

    # Marcadores cujo limiar mede o PREAMBULO, nao o ficheiro inteiro.
    ambito_preambulo = {m for m, kind, _, _, nota in wanted
                        if kind == "PREAMBLE" and AMBITO_PREAMBULO_RE.search(nota)}

    found = {marker: 0 for marker, _, _, _, _ in wanted}
    for path in chunks:
        text = path.read_text(errors="replace")
        head = ("\n".join(text.splitlines()[:preamble_end(text)])
                if ambito_preambulo else "")
        for marker in found:
            found[marker] += (head if marker in ambito_preambulo else text).count(marker)

    # D3: os mesmos marcadores, contados no codigo host versionado -- mas SO' os
    # de tipo TAG. Nao e' timidez, e' semantica: um marcador SYM/GLOBAL do
    # MANIFEST significa "DEFINIDO no preambulo injectado do lift" (ver main()),
    # e um `ps3_indirect_call` num hook e' um CALL SITE, nao uma definicao --
    # soma-lo mascararia divida real do lift com codigo que nunca a substituiu.
    # Uma TAG `[XXX]` nao tem esse problema: e' comportamento observavel, e a
    # casa dela e' onde o codigo estiver.
    # DIVIDA QUE FICA: quando um patch com marcadores SYM migrar para weak
    # override (o #4/`patch_b71_cb56c_reuse_block.py` e' o proximo candidato),
    # os `ps3_factory_*` vao sair do preambulo do lift e reaparecer aqui como
    # divida -- essa precisa de uma nota `MIGRADO:` no MANIFEST.tsv, do mesmo
    # feitio das `OBSOLETO:`/`GRUPO:` que ja existem. NAO esta fechada.
    tag_markers = {m for m, kind, _, _, _ in wanted if kind == "TAG"}
    host_files = resolve_host_sources(host_sources)
    from_host = {marker: 0 for marker in found}
    for path in host_files:
        text = path.read_text(errors="replace")
        for marker in tag_markers:
            from_host[marker] += text.count(marker)
    for marker in found:
        found[marker] += from_host[marker]

    obsolete_entries = [w for w in wanted if w[4].startswith("OBSOLETO")]
    grouped_entries = [w for w in wanted if GRUPO_RE.match(w[4])]
    obsolete_or_grouped = {w[0] for w in obsolete_entries} | {w[0] for w in grouped_entries}
    normal_entries = [w for w in wanted if w[0] not in obsolete_or_grouped]

    missing = [(m, k, n, found[m]) for m, k, n, _, _ in normal_entries if found[m] == 0]
    short = [(m, k, n, found[m]) for m, k, n, _, _ in normal_entries if 0 < found[m] < n]

    for marker, kind, want, got in missing:
        print(f"AUSENTE  {kind:6} {marker}  (esperado >={want}, encontrado 0)")
    for marker, kind, want, got in short:
        print(f"A MENOS  {kind:6} {marker}  (esperado >={want}, encontrado {got})")

    # D3: uma migracao tem de APARECER como migracao. Contar os hooks em
    # silencio trocaria uma divida falsa por um verde opaco -- pior negocio.
    migrados = sorted((m, from_host[m]) for m in from_host if from_host[m] > 0)
    if host_files:
        print(f"\nhost (TAGs migradas, codigo versionado): {len(host_files)} ficheiro(s) "
              f"-- {', '.join(p.name for p in host_files)}")
        for marker, n_host in migrados:
            print(f"  MIGRADO  {marker}: {found[marker] - n_host} no lift "
                  f"+ {n_host} em host = {found[marker]}")
        if not migrados:
            print("  (nenhum marcador do MANIFEST vive nestes ficheiros)")

    # AMBITO:preambulo -- medir num ambito diferente em silencio seria trocar
    # um limiar errado por um verde opaco. Cada marcador com a nota diz-se.
    for marker in sorted(ambito_preambulo):
        want = next(n for m, _, n, _, _ in wanted if m == marker)
        print(f"AMBITO   PREAMBLE {marker}: medido no PREAMBULO "
              f"(nao no ficheiro inteiro) = {found[marker]} (esperado >={want})")

    # OBSOLETO (T-03-08 mitigado): nunca falha o gate, seja found==0 ou >0.
    for marker, kind, want, chunks_s, nota in obsolete_entries:
        razao = nota[len("OBSOLETO"):].lstrip(":").strip()
        print(f"OBSOLETO  {kind}  {marker} -- {razao}")

    # GRUPO (D-3.1c): soma de found/min_count decide, nao cada marcador isolado.
    groups: dict[str, list[tuple[str, str, int, int]]] = defaultdict(list)
    for marker, kind, want, chunks_s, nota in grouped_entries:
        gid = GRUPO_RE.match(nota).group(1)
        groups[gid].append((marker, kind, want, found[marker]))

    group_failed = 0
    group_passed = 0
    for gid, items in sorted(groups.items()):
        sum_want = sum(want for _, _, want, _ in items)
        sum_found = sum(got for _, _, _, got in items)
        if sum_found < sum_want:
            group_failed += 1
            names = "+".join(m for m, _, _, _ in items)
            print(
                f"A MENOS (grupo)  {gid}: {names} "
                f"esperado>={sum_want} encontrado={sum_found}"
            )
        else:
            group_passed += 1

    total = len(wanted)
    ok = total - len(missing) - len(short) - len(obsolete_entries) - len(grouped_entries)
    print(f"\nbaseline: {ok}/{total} marcadores intactos, "
          f"{len(missing)} ausentes, {len(short)} a menos")
    print(
        f"obsoletos: {len(obsolete_entries)} citados (nunca falham o gate) | "
        f"grupos: {group_passed} passaram, {group_failed} falharam "
        f"(de {len(groups)} grupos, {len(grouped_entries)} marcadores)"
    )
    return 1 if (missing or short or group_failed) else 0


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    lift = Path(sys.argv[1])
    chunks = sorted(lift.glob("ppu_recomp_*.cpp"))
    if not chunks:
        print(f"sem chunks em {lift}", file=sys.stderr)
        return 2

    if "--preamble-counts" in sys.argv:
        # O limiar de um marcador `AMBITO:preambulo` tem de ser reproduzivel por
        # quem le a linha do MANIFEST -- senao o 56 e' um numero de fe. Imprime,
        # por simbolo do preambulo puro: contagem NO PREAMBULO e no ficheiro
        # INTEIRO (a antiga), para os dois numeros poderem ser comparados.
        pure_text = PURE.read_text(errors="replace")
        syms = sorted((set(SYM_RE.findall(pure_text)) | set(GLOBAL_RE.findall(pure_text)))
                      - {"ps3_timebase_now"})
        no_preamb = {s: 0 for s in syms}
        no_ficheiro = {s: 0 for s in syms}
        for path in chunks:
            text = path.read_text(errors="replace")
            head = "\n".join(text.splitlines()[:preamble_end(text)])
            for s in syms:
                no_preamb[s] += head.count(s)
                no_ficheiro[s] += text.count(s)
        print(f"# gen_manifest.py --preamble-counts {lift}")
        print("# simbolo\tno_preambulo\tno_ficheiro_inteiro")
        for s in syms:
            print(f"{s}\t{no_preamb[s]}\t{no_ficheiro[s]}")
        return 0

    if "--verify" in sys.argv:
        # D3: fontes host a contar TAMBEM. Default = games/gow2/hooks/ -- tem de
        # ser o default, e nao um opt-in, porque verify_lift.sh (o caminho de
        # aceite) invoca este script sem flags nenhumas.
        if "--no-host-sources" in sys.argv:
            host = []
        else:
            host = [sys.argv[i + 1] for i, a in enumerate(sys.argv)
                    if a == "--host-sources"]
            if not host:
                host = [str(HOST_SOURCES_DEFAULT)]
        return verify(chunks, Path(sys.argv[sys.argv.index("--verify") + 1]), host)

    pure_lines = set(PURE.read_text(errors="replace").splitlines())
    pure_text = PURE.read_text(errors="replace")
    # simbolos e globais que o proprio lifter emite -- nao sao injeccao nossa
    lifter_syms = set(SYM_RE.findall(pure_text)) | set(GLOBAL_RE.findall(pure_text))
    # D-2.1 (02-CONTEXT.md, Fase 2): dos 3 identificadores do preambulo puro,
    # so' 2 ganham marcador kind=PREAMBLE. ps3_timebase_now fica EXCLUIDO --
    # foi superseded pelo contrato ppu_timebase_now (Fase 1, commit e441f37,
    # runtime/syscalls/sys_timer.c:80-101). So' sobrevive hoje numa probe do
    # chunk 000 de uma arvore de safra mista; gatea-lo produziria alarme
    # falso num re-lift limpo, nao uma regressao real.
    preamble_syms = lifter_syms - {"ps3_timebase_now"}

    counts: dict[tuple[str, str], int] = defaultdict(int)
    origin: dict[tuple[str, str], set[str]] = defaultdict(set)

    for path in chunks:
        name = path.name
        text = path.read_text(errors="replace")
        lines = text.splitlines()
        end = preamble_end(text)
        injected = [ln for ln in lines[:end] if ln not in pure_lines]

        # TAGs: procuradas no ficheiro INTEIRO (os call sites vivem nos corpos)
        for tag in TAG_RE.findall(text):
            counts[("TAG", tag)] += 1
            origin[("TAG", tag)].add(name)

        # SYM/GLOBAL: so' o que e' DEFINIDO no preambulo injectado
        blob = "\n".join(injected)
        for sym in set(SYM_RE.findall(blob)) - lifter_syms:
            counts[("SYM", sym)] += text.count(sym)
            origin[("SYM", sym)].add(name)
        for gvar in set(GLOBAL_RE.findall(blob)) - lifter_syms:
            counts[("GLOBAL", gvar)] += text.count(gvar)
            origin[("GLOBAL", gvar)].add(name)

        # PREAMBLE: confirma que o preambulo puro (nao-injectado) SOBREVIVE
        # no chunk alvo. Diferente do bloco SYM/GLOBAL acima -- conta contra
        # o `text` do ficheiro INTEIRO, nao contra `blob`/`injected`, porque
        # o objectivo aqui nao e' detectar injeccao, e' confirmar sobrevivencia.
        for sym in preamble_syms:
            if sym in text:
                counts[("PREAMBLE", sym)] += text.count(sym)
                origin[("PREAMBLE", sym)].add(name)

    print("# MANIFEST do baseline forense do lift -- RDY-0 Task 1")
    print(f"# gerado por gen_manifest.py a partir de {lift.name}")
    print("# marcador\ttipo\tmin_count\tchunks_origem")
    for (kind, marker), n in sorted(counts.items(), key=lambda kv: (kv[0][0], kv[0][1])):
        chunks_s = ",".join(sorted(origin[(kind, marker)]))
        print(f"{marker}\t{kind}\t{n}\t{chunks_s}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
