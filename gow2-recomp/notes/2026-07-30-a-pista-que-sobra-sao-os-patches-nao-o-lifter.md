# A pista que sobra são os patches, não o lifter

**Data:** 2026-07-30 · Escrito depois de a Fase 6 fechar sem nomear a causa.
**Estado:** pista NÃO testada. Não é conclusão.

## Porque o lifter deixou de ser o suspeito principal

A Fase 6 mediu três hipóteses e refutou-as todas. Duas medições em particular apontam
para fora do lifter:

1. **A reversão cirúrgica de TOCFIX + `ctx->lr` não mudou nada.** Revertidos os 9 sítios
   dentro de `func_00147038` (`thr_auto_load`), rebuild de um chunk, binário construído:
   `REGRESSAO` idêntica (`startseq=1`, `thr_end=0`).
2. **O lift é byte-a-byte idêntico onde interessa.** O corpo de `func_00147038` e a sua
   vizinhança de 3 níveis de chamada são idênticos entre o lifter que gerou o último
   binário OK (`145fe58`) e o de hoje, que gera a produção que falha.
3. **A contagem de funções cresce monotonicamente** ao longo de toda a janela — nunca cai.

Se o código gerado para o caminho crítico é o mesmo nos dois lados, a diferença está
noutro sítio.

## O que ninguém testou

A cadeia de produção do binário tem três andares, e só se olhou para o primeiro:

```
ppu_lifter.py  →  apply_all_patches.sh (89 patches)  →  build_macos.sh  →  boot_gow2
     ^                      ^
  testado             NUNCA TESTADO
```

E a janela contém um commit que mexe exactamente aí:

```
e6d65a2  25 Jul 18:21  os 2 checks que falhavam num lift limpo passam
                       ^-- ANTES do boot_gow2.pre_v3 (18:23), que FUNCIONA
04dd546  25 Jul 19:13  repara 30 dos 41 patches que falhavam num lift limpo
                       (workflow, 7 agentes)  -- 39 ficheiros patch_*.py alterados
                       ^-- DEPOIS do ultimo binario OK
d587c93  26 Jul 00:42  instaladores para 9 dos 11 orfaos
f9ec60f  26 Jul 01:29  patch_zz_host_api_decls
```

**`04dd546` alterou 39 ficheiros `patch_*.py` de uma só vez**, produzidos por sete agentes
em paralelo, 50 minutos depois do último binário que se sabe funcionar.

O objectivo do commit era legítimo — fazer os patches aplicarem-se a um lift limpo, que é
metade do marco v1.0. Mas 39 patches reescritos numa leva, contra um lift que na altura
ainda não tinha sido validado in-boot, é exactamente o perfil de mudança que introduz uma
regressão silenciosa.

## Como testar isto, e é barato

**Não é preciso build.** A pergunta é se os patches produzem um lift diferente:

```bash
cd ../gow2-recomp
# 1. um lift limpo, duas vezes
python3 ../ps3recomp/tools/ppu_lifter.py EBOOT.ELF --functions functions.json \
        -o /tmp/lift_A -j 4
cp -r /tmp/lift_A /tmp/lift_B

# 2. aplicar os patches de HOJE a um, e os de e6d65a2 ao outro
./apply_all_patches.sh /tmp/lift_A
git stash push -u -- recomp_mid_v2/          # guardar o estado actual
git checkout e6d65a2 -- recomp_mid_v2/
./apply_all_patches.sh /tmp/lift_B
git checkout HEAD -- recomp_mid_v2/ && git stash pop

# 3. diffar
diff -r /tmp/lift_A /tmp/lift_B | head -50
```

Se o diff for vazio, os patches não são a causa e a pista morre por ~4 minutos de trabalho.
Se não for, o diff **nomeia os ficheiros e as linhas** — que é literalmente o que o REG-02
pede e que a Fase 6 não conseguiu entregar pelo lado do lifter.

## Ressalvas

- **Isto é uma pista, não um resultado.** Não foi testado. Está aqui porque a Fase 6
  fechou sem causa e esta é a lacuna estrutural mais óbvia que sobrou.
- O `git stash` no `recomp_mid_v2/` tem de ser feito com cuidado: esse directório tem
  ficheiros gitignored (o lift) e WIP de outras sessões. Usar `git worktree` ou uma cópia
  do repositório é mais seguro do que mexer no checkout activo.
- Reverter 39 patches de uma vez responde "sim/não", não "qual". Se der diferença, o passo
  seguinte é bissectar dentro dos 39 — o que é rápido, porque cada aplicação são segundos.
