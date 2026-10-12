#!/usr/bin/env bash
# promote_lift.sh -- o procedimento de promocao de producao (D-5.3, 05-CONTEXT.md).
#
# Ate ao Plano 05-04, a promocao de um lift regenerado a producao
# (recomp_macos_v2 / boot_gow2) tinha sido feita a MAO, uma vez, a
# 2026-07-26 -- o recomp_macos_v2 atual E' o lift regenerado dessa vez, e o
# lift/binario anteriores ficaram preservados em recomp_macos_v2.pre_v4 /
# boot_gow2.pre_v4. Essa promocao a mao nao estava escrita em lado nenhum.
# Este script formaliza-a:
#
#   1. GATE OBRIGATORIO: corre accept_relift.sh (Plano 05-03, aceite de tres
#      pernas D-5.1 + contadores por delta D-5.2) ANTES de tocar em
#      qualquer coisa. Se o aceite nao passar (rc!=0), a promocao aborta --
#      nunca prossegue "so com base no smoke", que foi exatamente o erro de
#      2026-07-26 (36 conversoes OPD em falta entraram em producao porque o
#      gate de entao so media a intro).
#   2. BACKUP POR RENOME, nunca copia nem apagar: recomp_macos_v2 e
#      boot_gow2 anteriores sao renomeados para .pre_<SUFFIX> (mv, nao rm,
#      nao cp destrutivo) -- o lift ronda os 400MB, renomear e' O(1) e nunca
#      arrisca uma janela de inconsistencia como copiar copiaria.
#   3. REBUILD do boot_gow2 a partir do novo recomp_macos_v2
#      (FORCE_REBUILD_LIFT=1 -- nunca testar um binario desactualizado).
#   4. CONFIRMACAO POS-BUILD via smoke_relift_equiv.sh --bin (o aceite testou
#      o LIFT; esta confirmacao testa o BINARIO reconstruido, que pode
#      divergir do lift se o rebuild introduzir algo -- caso raro mas
#      possivel).
#   5. REVERSAO AUTOMATICA se o rebuild OU a confirmacao pos-build falhar --
#      a producao NUNCA fica sem um boot_gow2 executavel.
#   6. Um modo --revert <SUFFIX> explicito, para desfazer uma promocao ja
#      concluida (ou interrompida), provado por HASH (nao por confianca)
#      contra o que ficou registado em PROMOTION_LOG.tsv.
#   7. PROMOTION_LOG.tsv -- registo append-only (nunca sobrescrito) de cada
#      promocao/reversao, para auditoria.
#
# Nunca e' um efeito colateral de um build normal -- so corre quando
# invocado explicitamente, com o lift candidato como argumento.
#
# LICAO (encontrada durante a rehearsal real desta task, 2026-07-29): uma
# promocao pode ser interrompida por algo externo ao script (kill -9,
# falha de maquina, sessao fechada) a MEIO das operacoes -- depois do
# backup por rename e da copia do candidato, mas antes de o script proprio
# decidir se teve sucesso ou falhou. Na primeira versao deste script, a
# linha em PROMOTION_LOG.tsv so era escrita no FIM (sucesso ou falha
# interna) -- uma interrupcao externa nesse meio-termo deixava producao
# potencialmente sem boot_gow2 executavel E sem NENHUM registo no log, o
# que impedia o --revert de confirmar por hash (so' tinha o AVISO "sem
# hash esperado para comparar"). Corrigido: a partir desta versao, o log
# tem TRES estados por promocao, nunca dois:
#   PROMOTE-BEGIN  -- escrito IMEDIATAMENTE apos o gate passar e os hashes
#                     de origem serem capturados, ANTES de qualquer mv/cp
#                     em producao. E' este registo que sobrevive a uma
#                     interrupcao externa a meio.
#   PROMOTE-OK     -- escrito so no fim, se TUDO (copia + rebuild +
#                     confirmacao) tiver sucesso.
#   PROMOTE-FAIL   -- escrito quando o proprio script deteta uma falha
#                     interna (copia/rebuild/confirmacao) e aciona a
#                     reversao automatica, ANTES de chamar do_revert().
# hash_v2_antes/hash_boot_antes sao IDENTICOS nas tres linhas de uma mesma
# promocao (capturados uma so vez, no PROMOTE-BEGIN) -- por isso o
# --revert so precisa de encontrar QUALQUER uma das tres para ter o hash
# esperado, mesmo que a promocao nunca tenha chegado a um estado final.
#
# Uso:
#   ./promote_lift.sh NEW_LIFT_DIR [--yes] [--patch-status TSV_DA_ETAPA_4]
#     --patch-status (ou ACCEPT_PATCH_STATUS no ambiente) e' OBRIGATORIO desde
#     a correccao do defeito D2 (2026-08-03): a perna 3 do accept_relift.sh
#     julga o TSV da PRIMEIRA passagem do apply_all_patches.sh -- a corrida que
#     produziu o lift candidato -- e nao o resultado de uma segunda passagem
#     sobre a arvore ja' patchada. Este script so' o reencaminha.
#     Promove NEW_LIFT_DIR a producao. Sem --yes, pede confirmacao
#     interativa ("digite 'sim'") antes de tocar em qualquer coisa. Com
#     --yes, prossegue sem perguntar (para uso nao-interativo/rehearsal).
#
#   ./promote_lift.sh --revert SUFFIX
#     Desfaz a promocao identificada por SUFFIX (o mesmo que aparece nos
#     nomes recomp_macos_v2.pre_<SUFFIX> / boot_gow2.pre_<SUFFIX> e nas
#     linhas PROMOTE-BEGIN/PROMOTE-OK/PROMOTE-FAIL de PROMOTION_LOG.tsv).
#     Confirma por hash que o estado restaurado bate com o que foi
#     registado no PROMOTE-BEGIN -- funciona mesmo que a promocao tenha
#     sido interrompida antes de chegar a um PROMOTE-OK/PROMOTE-FAIL.
#
# Variaveis:
#   PROMO_SUFFIX   override do sufixo (default: timestamp real) -- permite
#                  determinismo em reexecucoes de teste/rehearsal.
#   PS3_ENGINE_ROOT override da raiz do motor ps3recomp (default: ../ps3recomp
#                  relativo a este script), a mesma convencao de
#                  accept_relift.sh / apply_all_patches.sh.
#
# Protocolo G6 (D-5.5): kill sempre por PID dentro dos scripts subjacentes
# (smoke_relift_equiv.sh), nunca pkill -f boot_gow2. Logs de confirmacao em
# /tmp, fora dos dois repositorios.
set -uo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")" || exit 1
REPO="$PWD"
PS3_ENGINE_ROOT="${PS3_ENGINE_ROOT:-$REPO/../ps3recomp}"

# ---- hash_dir(): copiada literalmente de games/gow2/test_relift_build.sh:20-22
hash_dir() {
    find "$1" -type f -print0 | sort -z | tar --null -T - -cf - 2>/dev/null | md5 -q
}

# ---- hash de um ficheiro unico (o binario boot_gow2). Trata explicitamente
#      o caso do ficheiro nao existir (ex.: uma promocao interrompida entre
#      o backup-rename e o rebuild deixa boot_gow2 ausente por um instante)
#      -- evita um erro cru do shasum e um campo vazio no log. -------------
hash_file() {
    if [ ! -f "$1" ]; then
        echo "(ficheiro ausente)"
        return 0
    fi
    md5 -q "$1" 2>/dev/null || shasum -a 256 "$1" | cut -d' ' -f1
}

LOG="$REPO/PROMOTION_LOG.tsv"

# ---- regista uma linha em PROMOTION_LOG.tsv (append-only, cria cabecalho
#      na primeira vez que o ficheiro nao existe) --------------------------
log_promotion() {
    # $1=acao $2=lift $3=sufixo $4=hash_v2_antes $5=hash_boot_antes
    if [ ! -f "$LOG" ]; then
        printf 'timestamp\tacao\tlift\tsufixo\thash_v2_antes\thash_boot_antes\n' > "$LOG"
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$1" "$2" "$3" "$4" "$5" >> "$LOG"
}

# ---- do_revert(suffix, preserve_tag, [expected_hash]): funcao unica de
#      reversao, reusada pelo caminho de falha automatica (rebuild/
#      confirmacao pos-build falharam a meio de uma promocao) E pelo modo
#      --revert explicito. NUNCA apaga -- o que estava em producao no
#      momento da reversao e preservado por rename sob .$preserve_tag.
#      Se expected_hash for fornecido, confirma por HASH que
#      recomp_macos_v2 restaurado bate com o esperado -- se nao bater, e'
#      um ERRO FATAL nao recuperavel automaticamente (imprime os dois
#      hashes e para, nao tenta adivinhar). ------------------------------
do_revert() {
    local suffix="$1" preserve_tag="$2" expected_hash="${3:-}"

    echo "=== do_revert: a restaurar backup .pre_${suffix}, preservando o estado atual sob .${preserve_tag} ==="

    if [ -d "$REPO/recomp_macos_v2" ]; then
        if ! mv "$REPO/recomp_macos_v2" "$REPO/recomp_macos_v2.${preserve_tag}"; then
            echo "ERRO FATAL: mv recomp_macos_v2 -> recomp_macos_v2.${preserve_tag} falhou -- estado nao recuperavel automaticamente" >&2
            return 1
        fi
    fi
    if [ -f "$REPO/boot_gow2" ]; then
        mv "$REPO/boot_gow2" "$REPO/boot_gow2.${preserve_tag}" 2>/dev/null || true
    fi

    if [ ! -d "$REPO/recomp_macos_v2.pre_${suffix}" ] || [ ! -f "$REPO/boot_gow2.pre_${suffix}" ]; then
        echo "ERRO FATAL: backup recomp_macos_v2.pre_${suffix} / boot_gow2.pre_${suffix} nao encontrado -- nao e' possivel restaurar automaticamente" >&2
        return 1
    fi

    if ! mv "$REPO/recomp_macos_v2.pre_${suffix}" "$REPO/recomp_macos_v2"; then
        echo "ERRO FATAL: mv recomp_macos_v2.pre_${suffix} -> recomp_macos_v2 falhou" >&2
        return 1
    fi
    if ! mv "$REPO/boot_gow2.pre_${suffix}" "$REPO/boot_gow2"; then
        echo "ERRO FATAL: mv boot_gow2.pre_${suffix} -> boot_gow2 falhou" >&2
        return 1
    fi

    local h_now
    h_now=$(hash_dir "$REPO/recomp_macos_v2")
    echo "hash_dir(recomp_macos_v2) apos revert = $h_now"

    if [ -n "$expected_hash" ]; then
        if [ "$h_now" = "$expected_hash" ]; then
            echo "REVERT CONFIRMADO POR HASH: recomp_macos_v2 identico ao estado pre-promocao (esperado=$expected_hash medido=$h_now)"
        else
            echo "ERRO FATAL: hash pos-revert NAO bate com o esperado -- esperado=$expected_hash medido=$h_now -- NAO prossiga, investigar manualmente antes de qualquer outra acao" >&2
            return 1
        fi
    else
        echo "AVISO: sem hash esperado para comparar (nenhum registo encontrado) -- revert executado, mas nao confirmado por hash." >&2
    fi

    return 0
}

# ---- modo de promocao -------------------------------------------------------
cmd_promote() {
    local NEW_LIFT_REL="${1:?uso: promote_lift.sh NEW_LIFT_DIR [--yes]}"
    shift || true
    local YES=0
    # TSV da ETAPA 4, exigido pela perna 3 do accept_relift.sh desde a
    # correccao do defeito D2 (2026-08-03). Passa-se tal e qual ao gate --
    # este script nunca o interpreta.
    local PATCH_STATUS="${ACCEPT_PATCH_STATUS:-}"
    while [ $# -gt 0 ]; do
        case "$1" in
            --yes)            YES=1; shift ;;
            --patch-status)   PATCH_STATUS="${2:-}"; shift 2 || shift ;;
            --patch-status=*) PATCH_STATUS="${1#*=}"; shift ;;
            *)                shift ;;
        esac
    done

    local NEW_LIFT="$REPO/${NEW_LIFT_REL#./}"
    if [ ! -d "$NEW_LIFT" ] || [ ! -f "$NEW_LIFT/ppu_recomp.h" ]; then
        echo "ERRO: $NEW_LIFT nao parece ser um lift valido (falta ppu_recomp.h) -- abortado, nada foi tocado" >&2
        exit 1
    fi

    # ---- GATE OBRIGATORIO (D-5.3): nada abaixo corre se isto falhar --------
    echo "=============================================================="
    echo " GATE OBRIGATORIO: accept_relift.sh $NEW_LIFT_REL"
    echo " (aceite de tres pernas D-5.1 + contadores por delta D-5.2)"
    echo "=============================================================="
    "$REPO/accept_relift.sh" "$NEW_LIFT_REL" 6 /tmp --patch-status "$PATCH_STATUS"
    local gate_rc=$?
    if [ "$gate_rc" != "0" ]; then
        echo "ABORTADO: accept_relift.sh recusou $NEW_LIFT_REL (rc=$gate_rc) -- promocao NAO prossegue" >&2
        exit 1
    fi
    echo "GATE PASSOU: accept_relift.sh rc=0 para $NEW_LIFT_REL -- prosseguindo com a promocao"

    if [ "$YES" != "1" ]; then
        echo
        echo "Vai promover '$NEW_LIFT_REL' a producao:"
        echo "  - backup por RENAME de recomp_macos_v2 / boot_gow2 (nunca apagados)"
        echo "  - copia de '$NEW_LIFT_REL' para recomp_macos_v2"
        echo "  - rebuild de boot_gow2 (FORCE_REBUILD_LIFT=1)"
        echo "  - confirmacao pos-build (smoke_relift_equiv.sh --bin, 6 corridas)"
        read -r -p "Confirmar promocao? (digite 'sim'): " reply
        if [ "$reply" != "sim" ]; then
            echo "ABORTADO pelo utilizador -- nada foi tocado." >&2
            exit 1
        fi
    fi

    local SUFFIX="${PROMO_SUFFIX:-$(date +%Y%m%d_%H%M%S)}"

    local H_V2_BEFORE H_BOOT_BEFORE
    H_V2_BEFORE=$(hash_dir "$REPO/recomp_macos_v2")
    H_BOOT_BEFORE=$(hash_file "$REPO/boot_gow2")
    echo "hash ANTES da promocao: recomp_macos_v2=$H_V2_BEFORE boot_gow2=$H_BOOT_BEFORE"

    # ---- PROMOTE-BEGIN: escrito ANTES de qualquer mv/cp em producao. Se o
    #      processo for interrompido a meio (kill externo, falha de
    #      maquina), esta linha e' o que sobra para o --revert confirmar
    #      por hash -- nunca deixa a auditoria vazia so porque a promocao
    #      nao chegou a um estado final. --------------------------------
    log_promotion "PROMOTE-BEGIN" "$NEW_LIFT_REL" "$SUFFIX" "$H_V2_BEFORE" "$H_BOOT_BEFORE"

    # ---- backup por RENAME (nunca rm, nunca cp destrutivo) ------------------
    echo "=== backup por rename (suffix=$SUFFIX) ==="
    if ! mv "$REPO/recomp_macos_v2" "$REPO/recomp_macos_v2.pre_${SUFFIX}"; then
        echo "ERRO FATAL: mv recomp_macos_v2 -> recomp_macos_v2.pre_${SUFFIX} falhou -- nada mais foi tocado, producao intacta" >&2
        exit 1
    fi
    if ! mv "$REPO/boot_gow2" "$REPO/boot_gow2.pre_${SUFFIX}"; then
        echo "ERRO FATAL: mv boot_gow2 -> boot_gow2.pre_${SUFFIX} falhou -- a reverter o recomp_macos_v2 ja movido antes de abortar" >&2
        mv "$REPO/recomp_macos_v2.pre_${SUFFIX}" "$REPO/recomp_macos_v2"
        exit 1
    fi

    # ---- copia do candidato para producao (cp, nao mv -- o candidato
    #      original permanece intacto para registo) ------------------------
    echo "=== copia do candidato ($NEW_LIFT_REL) para recomp_macos_v2 ==="
    if ! cp -R "$NEW_LIFT" "$REPO/recomp_macos_v2"; then
        echo "FALHA NA COPIA -- a reverter automaticamente" >&2
        log_promotion "PROMOTE-FAIL" "$NEW_LIFT_REL" "$SUFFIX" "$H_V2_BEFORE" "$H_BOOT_BEFORE"
        do_revert "$SUFFIX" "failed_${SUFFIX}" "$H_V2_BEFORE"
        exit 1
    fi

    # ---- rebuild -------------------------------------------------------------
    echo "=== rebuild de boot_gow2 a partir do novo recomp_macos_v2 (FORCE_REBUILD_LIFT=1) ==="
    OUT="$REPO/boot_gow2" FORCE_REBUILD_LIFT=1 "$REPO/build_macos.sh" "$REPO/recomp_macos_v2"
    local build_rc=$?
    if [ "$build_rc" != "0" ]; then
        echo "FALHA NO REBUILD (rc=$build_rc) -- a reverter automaticamente" >&2
        log_promotion "PROMOTE-FAIL" "$NEW_LIFT_REL" "$SUFFIX" "$H_V2_BEFORE" "$H_BOOT_BEFORE"
        do_revert "$SUFFIX" "failed_${SUFFIX}" "$H_V2_BEFORE"
        exit 1
    fi

    # ---- confirmacao pos-build (o gate testou o LIFT; isto testa o BINARIO
    #      reconstruido) ------------------------------------------------------
    echo "=== confirmacao pos-build (smoke_relift_equiv.sh --bin, 6 corridas) ==="
    "$REPO/smoke_relift_equiv.sh" --bin "$REPO/boot_gow2" 6 "/tmp/promote_confirm_${SUFFIX}.tsv"
    local confirm_rc=$?
    if [ "$confirm_rc" != "0" ]; then
        echo "CONFIRMACAO POS-BUILD FALHOU (rc=$confirm_rc) -- a reverter automaticamente (o build pode ter introduzido uma regressao que o accept_relift.sh nao viu, porque esse testou o LIFT, nao o binario reconstruido)" >&2
        log_promotion "PROMOTE-FAIL" "$NEW_LIFT_REL" "$SUFFIX" "$H_V2_BEFORE" "$H_BOOT_BEFORE"
        do_revert "$SUFFIX" "failed_${SUFFIX}" "$H_V2_BEFORE"
        exit 1
    fi

    log_promotion "PROMOTE-OK" "$NEW_LIFT_REL" "$SUFFIX" "$H_V2_BEFORE" "$H_BOOT_BEFORE"

    echo "=============================================================="
    echo " PROMOCAO CONCLUIDA"
    echo "=============================================================="
    echo "promovido : $NEW_LIFT_REL -> recomp_macos_v2 (suffix=$SUFFIX)"
    echo "backups   : recomp_macos_v2.pre_${SUFFIX} / boot_gow2.pre_${SUFFIX}"
    echo "hash antes: recomp_macos_v2=$H_V2_BEFORE boot_gow2=$H_BOOT_BEFORE"
    echo "reversao  : ./promote_lift.sh --revert ${SUFFIX}"
}

# ---- modo de reversao explicita --------------------------------------------
cmd_revert() {
    local REVERT_SUFFIX="${1:-}"
    if [ -z "$REVERT_SUFFIX" ]; then
        echo "uso: promote_lift.sh --revert SUFFIX" >&2
        exit 2
    fi
    if [ ! -d "$REPO/recomp_macos_v2.pre_${REVERT_SUFFIX}" ] || [ ! -f "$REPO/boot_gow2.pre_${REVERT_SUFFIX}" ]; then
        echo "ERRO: backup nao encontrado -- esperava $REPO/recomp_macos_v2.pre_${REVERT_SUFFIX} e $REPO/boot_gow2.pre_${REVERT_SUFFIX}" >&2
        exit 2
    fi

    local H_CURRENT_V2 H_CURRENT_BOOT
    H_CURRENT_V2=$(hash_dir "$REPO/recomp_macos_v2")
    H_CURRENT_BOOT=$(hash_file "$REPO/boot_gow2")
    echo "hash ATUAL (a ser substituido pelo revert): recomp_macos_v2=$H_CURRENT_V2 boot_gow2=$H_CURRENT_BOOT"

    local TS_NOW
    TS_NOW="$(date +%Y%m%d_%H%M%S)"

    # ---- verificacao por hash: procura em PROMOTION_LOG.tsv QUALQUER linha
    #      PROMOTE-* (BEGIN/OK/FAIL) com este sufixo, e usa o hash la
    #      registado como o "esperado". As tres variantes tem sempre o
    #      MESMO hash_v2_antes/hash_boot_antes (capturados uma so vez, no
    #      PROMOTE-BEGIN) -- por isso a primeira ocorrencia serve, mesmo
    #      que a promocao tenha sido interrompida antes de chegar a
    #      PROMOTE-OK/PROMOTE-FAIL. -------------------------------------
    local EXPECTED_V2="" EXPECTED_BOOT=""
    if [ -f "$LOG" ]; then
        read -r EXPECTED_V2 EXPECTED_BOOT < <(awk -F'\t' -v s="$REVERT_SUFFIX" '$2 ~ /^PROMOTE/ && $4==s {print $5, $6; exit}' "$LOG")
    fi
    if [ -z "$EXPECTED_V2" ]; then
        echo "AVISO: nenhuma linha PROMOTE-* com sufixo=$REVERT_SUFFIX encontrada em $LOG -- revert prossegue mas sem confirmacao por hash contra o registo." >&2
    fi

    if ! do_revert "$REVERT_SUFFIX" "reverted_${TS_NOW}" "$EXPECTED_V2"; then
        echo "ERRO FATAL: revert falhou ou o hash pos-revert nao bateu -- ver mensagens acima. Estado substituido preservado em recomp_macos_v2.reverted_${TS_NOW} / boot_gow2.reverted_${TS_NOW} (se chegou a esse ponto)." >&2
        exit 1
    fi

    log_promotion "REVERT" "(n/a)" "$REVERT_SUFFIX" "$H_CURRENT_V2" "$H_CURRENT_BOOT"

    echo "=============================================================="
    echo " REVERSAO CONCLUIDA"
    echo "=============================================================="
    echo "sufixo revertido : $REVERT_SUFFIX"
    echo "estado substituido preservado em: recomp_macos_v2.reverted_${TS_NOW} / boot_gow2.reverted_${TS_NOW}"
}

# ---- dispatch ---------------------------------------------------------------
case "${1:-}" in
    --revert)
        cmd_revert "${2:-}"
        ;;
    "")
        echo "uso: promote_lift.sh NEW_LIFT_DIR [--yes]  OU  promote_lift.sh --revert SUFFIX" >&2
        exit 2
        ;;
    *)
        cmd_promote "$@"
        ;;
esac
