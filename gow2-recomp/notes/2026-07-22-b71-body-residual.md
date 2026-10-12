# B71B8 exit + body1D4 residual (2026-07-22)

## Cadeia

```
B71B8 → … → 41D5C (WAD wait) → E4950 → 10F5E8 → C992C → 2A6608 → CBC20
  → alloc 0x19C → CBB2C → CB56C  ← HANG actual
  → … → +0x64=1 exit B71B8
```

## body1D4 residual — causa e progresso

| Achado | Evidência |
|--------|-----------|
| Residual `0x23DB43D0` | Header lixo (stream desync), não size legítimo |
| Desync pós-`~010` | Offline: a seguir vem `TXR_chestTexture1Chest` (sz=88); in-boot lia textura como header |
| Wrap ring | Guest E1480 faz split-copy; fill host **zerava avail** em wrap → bytes perdidos com `file_pos` já avançado |
| write_pos +4 | Fill só actualizava cursor/avail; E1254/E1290 usam +4 |

### Fixes (lift `recomp_macos_v2`)

1. **`F2B-STREAM-WRAP-COMPACT`** — compact wrap com temp buffer (não drop)
2. **write_pos (+4) = avail** após compact/fill
3. **`F2B-BODY-CLAMP`** — body ≤ stream_left
4. **`F2B-STREAM-DESYNC-STOP`** — type>0x100 ou body absurdo → state=0 rem=0 (same-class EOF-DONE)

### Smoke in-boot (`/tmp/b71_final_smoke.log` / `b71_desync_smoke`)

| Antes | Depois |
|-------|--------|
| T1 parava em `~010` | **T1 #74–80**: TXR_chest, GFX/PAL_Comicsmoke, ~00c, TXR_comicsmoke, **HealthChest** |
| BODY-CLAMP spam mid-FO | clamp mid ≈0; DESYNC-STOP só no fim (FO FULL) |
| WADTEX ~010 | score/decode in-boot |

**Ainda aberto (stream):** desync residual no fim do R_Perm (ASCII sizes em DESYNC-STOP); ideal achar offset exacto do primeiro header mau *após* HealthChest e se é wrap/cursor residual.

## B71B8 exit — **PASS** (`/tmp/b71_exit3.log`)

```
[POSTINTRO] exit func_000B71B8 #1 flag_obj=0x00543740 +0x64=1
```

| Step | Estado |
|------|--------|
| 41D5C → E4950+ | ✅ |
| R_Perm past ~010 | ✅ TXR/Comic/Health |
| CBC20 → CBB2C | ✅ alloc OK |
| CB56C | ✅ skip factory type 0x15 (vt lixo `0x5F436F75`) |
| B70B0 | ✅ (+ guard list-link) |
| 393E0 | ✅ **HLE-lite** (vtable only; full hangia) |
| icallA/B tail | ✅ SKIP se vt/code mau |
| **exit +0x64=1** | ✅ |

### Fixes B71 (além do stream)

| Peça | O quê |
|------|--------|
| CB56C | Factory table `0x868D48[0x54]` → ent com vt ASCII; skip se opd/code inválidos |
| B70B0 | Não escrever `next+4` se next null/low (OOB 0xFFFFFFEx) |
| 393E0 | Default HLE-lite; `PS3_B71_FULL_393E0=1` para caminho completo |
| B71 tail | Guard icallA/B iguais ao CB56C |

### Freelist

- Walk cap 10k; abort em node null/low

### Ainda aberto (pós-exit)

- Factory type 0x15 / tab `0x868D48` com vt corrupto (mesmo objecto SBP `0x401002F0`)
- 393E0 full (registry names) — hang; root TOC/string
- Stream DESYNC-STOP residual no fim do R_Perm
- Wall D / menu pixels

## Aceite desta leva

- [x] body residual explicado + stream past ~010
- [x] wrap-compact + write_pos + DESYNC-STOP
- [x] CB56C desbloqueado
- [x] **exit B71B8 in-boot** (`+0x64=1`)
- [x] 393E0 full natural (`PS3_B71_FULL_393E0=1` → `B71 after 393E0` + exit)
- [x] factory type 0x15 live (TYPE15 SNAP/REPAIR; CB56C sem SKIP) — stomp root still open

Veredito completo: `notes/2026-07-22-factory15-393e0-desync-walld.md`
