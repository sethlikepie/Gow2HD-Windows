# Oracle session: RPCS3 ↔ recomp boot diff

Practical procedure to turn “RPCS3 can play this” into **one implementable gap**
in ps3recomp. Companion code:

| Tool | Role |
|------|------|
| [`oracle_parse_recomp_log.py`](oracle_parse_recomp_log.py) | Parse `boot_gow2` logs → markdown checklist |
| `gow2-recomp/oracle_intro_checklist.sh` | Run GoW2 boot + write `oracle_out/checklist_*.md` |
| [`docs/RPCS3_AS_REFERENCE.md`](../RPCS3_AS_REFERENCE.md) | Theory, folder map, anti-patterns |

---

## 1. One-shot (GoW2 intro / media wall)

```bash
cd /path/to/gow2-recomp
chmod +x oracle_intro_checklist.sh
./oracle_intro_checklist.sh 30
# optional: attach an RPCS3 log you already saved
./oracle_intro_checklist.sh 30 ~/Library/Application\ Support/rpcs3/RPCS3.log
```

Outputs:

```text
oracle_out/recomp_<timestamp>.log      # raw boot
oracle_out/checklist_<timestamp>.md    # 10-row table + open list
```

Open the checklist, fill the **RPCS3 (fill)** column, pick **one** gap.

Full probes (if lift patches for SNDOPEN/FIOSOPEN are applied):

```bash
PS3_ORACLE_FULL_PROBES=1 ./oracle_intro_checklist.sh 40
```

---

## 2. Parse an existing recomp log only

```bash
cd /path/to/ps3recomp
python3 tools/oracle_parse_recomp_log.py /tmp/recomp_boot.log -m
python3 tools/oracle_parse_recomp_log.py /tmp/recomp_boot.log -o /tmp/checklist.md
python3 tools/oracle_parse_recomp_log.py /tmp/recomp_boot.log --json /tmp/facts.json
```

---

## 3. RPCS3 side (manual, 5 minutes)

1. Same game dump / decrypted content as `EBOOT.ELF` + `extracted/`.
2. Config → Log: raise `sys_fs`, `cellFs`, `cellAudio`, `cellVdec`, `cellSpurs`.
3. Boot only through the hang (intro logo).
4. Save log; grep:

```bash
grep -iE 'open|smlogo|sm_logo|\.wav|\.m2v|cellFs|sys_fs|snd|vdec' RPCS3.log | head -100
```

5. Paste into the checklist table.

Sparse clone of source (optional, for reading implementations):

```bash
git clone --depth 1 --filter=blob:none --sparse \
  https://github.com/RPCS3/rpcs3.git ../_ref_rpcs3
cd ../_ref_rpcs3
git sparse-checkout set rpcs3/Emu/Cell/lv2 rpcs3/Emu/Cell/Modules rpcs3/Emu/RSX
```

---

## 4. Decision rules

| Auto column says… | Likely action |
|-------------------|---------------|
| No SmLogo / wav in recomp log | Guest never called our cellFs/movie_io — need FIOS/snd path or different NID |
| `only psarc = yes` | Member open missing; compare RPCS3 member path |
| `movie_io served = no` but path in log | Resolve/cache miss — case, prefix, `PS3_MOVIE_CACHE` |
| `snd_stream` fail print | Path exists in cache but open failed earlier in guest |
| lv2 stubs high | Implement sc N using `lv2.cpp` BIND_SYSC + `sys_*.cpp` |
| unresolved NID | Grep NID in RPCS3 `Modules/` |

**Never:** host-write `st620` / EOS just to match RPCS3 progress (M2 lesson: measure EA/path first).

---

## 5. Session complete when

1. Checklist markdown exists with recomp auto column filled.
2. RPCS3 column has at least the open order for the hang window.
3. **One** written next action (file + function or env).
4. After the fix, re-run `oracle_intro_checklist.sh` and show the auto column changed.
