# FlashUI residual + sparse pós-hold (2026-07-22)

## Sparse pós-hold — classificado

| Sinal | Valor |
|-------|--------|
| Bind EAs | só `C0FB0980` / `C1021180` / `C0F40180` 1280×720 DXT1 |
| Novos binds pós-R_Perm | **0** (pad autostart, 30s+) |
| VRAM DXT1 full | denso nz≈234k/460800 (legal-scale) |
| Frame guest pós-DONE | nb≈0.013 center≈(255,254,255) |

**Veredito:** o guest **pinta** fullscreen (draw path OK), mas o conteúdo
ligado é o **último HD DXT1 de boot** (logo empresa residual / sparse),
**não** UI de menu. Sem novos texels/EAs de menu nesta fase.

Viewport retina: fix de escala 720→drawable (ps3recomp metal).

## FlashUI / CRC

| Sinal | Valor |
|-------|--------|
| Default.ps3fx CRC-LK | **hit=1**, str=`TEXTURE=1;CONSTCOLOR=1;…` (dezenas×) |
| Invalid Default spam | **0** (c/ HOSTRES) |
| FlashUI | **1×** `Unknown combination string` (sem string de props) |
| Momento FlashUI | cedo, após HOSTRES `flashui.cfx`, **antes** de o pipeline
  de combination Default estar estável no log |

FlashUI falha com **combination vazia/desconhecida** (mensagem sem
`TEXTURE=…`), não com uma combo válida miss no mapa. Default está
saudável no consumer CRC.

## Implicação menu

1. **Não** é “só falha FlashUI” a bloquear o ecrã — o ecrã guest mostra
   o last-bound HD tex.
2. Menu precisa de **fase** que:
   - carregue UI textures (novos binds ≠ C0FB*), e/ou
   - recompile FlashUI com combination real (material props),
   - avance FSM (pad/level) além do idle pós-logo.
3. OPD 534CB8 / A1 walk **continuam 0** — sem factories de cena.

## Próximo discriminador

1. **Quem deveria rebindar texturas de UI** após R_Perm / pad START.
2. **Combination FlashUI** — capturar string real (vazia?) no site do
   lookup quando o path é FlashUI (probe CRC-LK alargado a miss + path
   name).
3. Manter draw path; **não** CRC bypass.

## Recipe

```bash
export PS3_TRACE_CRCLK=1 PS3_TRACE_TEX_PIXELS=1 PS3_METAL_VP_OFF=1
export PS3_RSX_BACKEND=metal PS3_RSX_FIFO=1
# pós DONE: guest_present draws=1; binds só C0FB*; FlashUI 1× unknown
```
