# VRAM guest fill discriminator (pós-OPD-morto) — 2026-07-22

## Pergunta

Quem preenche RSX local `0xC0FBxxxx` (binds HD DXT1)? A nota A1
(`a1-menu-natural-wall`) assumia **VRAM vazia** (`TEX-PX nonzero=0`). Isso
bloqueia o menu natural?

## Probes

| Env | O quê |
|-----|--------|
| `PS3_TRACE_VRAM=1` | `[VRAM-SCAN]` ocupação local 256 MB (1 MB slabs + binds full + scratch) |
| `PS3_TRACE_TEX_PIXELS=1` | activa VRAM-SCAN + amostra no bind |
| `PS3_RSX_STATS=1` | histograma de methods FIFO |

Hooks: `ps3_scan_vram_occupancy(phase)` em `ppu_loader` (phase=0 pós-R_Perm) e
`cellGcmSetFlipCommand` (phase=1 @flip200, phase=2 @flip800). Read-only; OFF default.

## Boot natural (Metal, EOS natural, ~25–30 s → R_Perm full)

Log: `/tmp/vdec_vram_late.log` (+ `/tmp/vdec_vram_scan.log`).

### Ocupação local (idêntico phase 1 / 2 / 0 pós-R_Perm)

| Sinal | Valor |
|-------|--------|
| hot_slabs (1 MB) | **3** de 256 |
| first_hot | `0xC0300000` (sample_nz sobe 100→400→**768** ao longo do boot) |
| slab `0xC0F00000` | sample_nz=327 max=255 (estável) |
| slab `0xC1000000` | sample_nz=38 max=255 |
| bind `0xC0FB0980` DXT1 full | **nz=234348/460800 max=255** |
| bind `0xC1021180` | nz=233726/460800 max=255 |
| bind `0xC0F40180` | nz=254270/460800 max=255 |
| hostres scratch `0x4B000000` | nz=226001 head=`EFCT` (scratch reutilizado; não é o ctxr residual) |
| `texture uploaded` Metal BC1 | **sim** (ea=C0FB0980, 1280×720 fmt=0x86) |
| TEX-PX sample 8 KiB | nz=4096/8192 max=170 (estável todo o legal/movie) |

### FIFO (flip #500)

Methods NV4097 clássicos: surface/viewport/texture `0x1A00+`, draw `0x1808`/`0x1820`.
**Sem** evidência de motor de transfer separado a ser necessário neste path — os
texels já estão em local quando o bind corre. Runtime **não** tem HLE
`cellGcmSetTransfer*`; o fill legal chega por **write PPU** (ou path já coberto).

### Pós-R_Perm

- R_PermA full `bytes_read=20169344` observado.
- VRAM-SCAN phase=0: **mesmos** 3 binds HD; **zero** novos EAs de UI/menu.
- C030 slab continua a aquecer (FB/label?), mas não há texturas de menu novas.

## Vereditos

| Hipótese | Estado |
|----------|--------|
| Guest local dos binds HD está zerado (TEX-PX vazio) | **REFUTADA** neste boot — dados DXT1 densos + upload Metal OK |
| Falta de SetTransfer HLE impede *qualquer* fill de legal | **REFUTADA** para o path legal (dados chegam sem transfer HLE) |
| Menu/UI materializa texturas novas pós-R_Perm nesta janela | **REFUTADA** (só os 3 EAs HD de suporte) |
| Parede do menu = “VRAM vazia genérica” | **Incorreta** — legal preenche; **menu não agenda assets novos** |
| OPD factory 534CB8 / micro-ctor | **Continua morto** (nota opd-534cb8-dead) — ortogonal a este fill |

### Correcção à nota A1

`2026-07-22-a1-menu-natural-wall.md` §2 (“VRAM guest vazia”) aplica-se a uma
medição TEX-PX antiga com max=0; **reprodução actual** mostra fill estável nos
mesmos EAs. A conclusão de produto mantém-se: **menu natural não alcançado**,
mas a causa *não* é “nada escreve em `C0FB*`”.

## Implicação (próximo discriminador ordenado)

1. **Guest draw do legal após hold clear** — com dados + upload OK, o present
   pós-logos deveria mostrar o quad texturado *se* o path guest present não
   estiver a clear/preto. Dump `PS3_FRAME_DUMP` após `boot logo queue DONE`
   sem content-hold (já limpo) para classificar LEGAL vs PRETO.
2. **Progressão FSM/pad/level** que carregue UI (novos binds ≠ C0FB/C102/C0F4)
   — factory OPD morto implica outro entry (ou fase nunca atingida).
3. **Não** priorizar SetTransfer HLE como desbloqueio do legal; só se RE
   mostrar transfer para assets de *menu* que fiquem em main sem local.

## Recipe

```bash
export PS3_TRACE_VRAM=1   # ou PS3_TRACE_TEX_PIXELS=1
export PS3_RSX_BACKEND=metal PS3_RSX_FIFO=1
# sem GATE_FORCE / SHADER_DEMO
./boot_gow2 EBOOT.ELF
grep -E 'VRAM-SCAN|TEX-PX|texture uploaded|R_Perm' log
```

## Código

- `ps3recomp/runtime/ppu/ppu_loader.cpp` — `ps3_scan_vram_occupancy`
- `ps3recomp/libs/video/cellGcmSys.c` — scan @ flip 200/800
