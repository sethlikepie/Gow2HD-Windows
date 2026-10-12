# RPCS3 como oráculo no port GoW2

O guia completo vive no motor:

**[`../ps3recomp/docs/RPCS3_AS_REFERENCE.md`](../../ps3recomp/docs/RPCS3_AS_REFERENCE.md)**

## One-shot (intro / wav / FIOS)

```bash
cd gow2-recomp
./oracle_intro_checklist.sh 30
# opcional: segundo arg = log do RPCS3
./oracle_intro_checklist.sh 30 /path/to/RPCS3.log
```

Gera `oracle_out/checklist_*.md` com a tabela de 10 linhas (coluna recomp
automática; coluna RPCS3 para preencheres). Procedimento:
[`../ps3recomp/tools/oracle_diff_boot.md`](../../ps3recomp/tools/oracle_diff_boot.md).

## Manual (resumo)

1. Correr o **mesmo** `EBOOT.ELF` no [RPCS3](https://github.com/RPCS3/rpcs3) com log alto de FS/audio/vdec/spurs.
2. Correr o checklist acima (ou `boot_gow2` + `oracle_parse_recomp_log.py`).
3. Preencher a tabela de gaps (ordem de open, path exacto, módulo, handle).
4. **Não** forjar `st620` / EOS só porque o emulador avançou.
5. Antes de meses de HLE: confirmar se o bug não é **lift** (lição M2 / rA=0).

Clone local opcional (fora do git do port):

```bash
git clone --depth 1 --filter=blob:none --sparse \
  https://github.com/RPCS3/rpcs3.git ../_ref_rpcs3
cd ../_ref_rpcs3 && git sparse-checkout set rpcs3/Emu/Cell/lv2 rpcs3/Emu/Cell/Modules rpcs3/Emu/RSX
```
