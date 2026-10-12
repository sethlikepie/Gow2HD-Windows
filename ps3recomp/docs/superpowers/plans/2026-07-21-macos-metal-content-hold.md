# Mac Metal — content hold / HOSTRES sticky (M8)

> **For agentic workers:** `subagent-driven-development` ou `executing-plans`.  
> **Depende de:** present Metal (M0 movie API ou M1 blit).  
> **Ref Win:** `rsx_d3d12_content_hold`, `rsx_d3d12_force_bind_rgba`, pause durante movie HLE.

**Goal:** Portar o **content hold** (textura HOSTRES / legal screen sticky) e o pause enquanto o movie HLE corre, para o Mac não perder o comportamento Windows de “segurar” um fullscreen RGBA entre flips.

**Architecture:**

```
HOSTRES decode (já no host se existir) → rsx_metal_content_hold(rgba,w,h)
                                       → staging igual movie overlay
movie_playback_begin → pause hold
movie_stop / end     → re-arm se PS3_CONTENT_HOLD_AFTER_MOVIE
```

API espelho:

```c
void rsx_metal_content_hold(const void* rgba, unsigned w, unsigned h);
void rsx_metal_force_bind_rgba(unsigned unit, const void* rgba, unsigned w, unsigned h); /* opcional M3 */
```

Quem chama no Win: seguir `rg content_hold` e portar call sites com `#ifdef __APPLE__` ou função comum `rsx_host_content_hold` que despacha ao backend activo.

## Global Constraints

- Não commitar PNG de legal screen do jogo.
- Coordenar com M0: movie tem prioridade visual (paridade Win `s_content_hold_paused`).
- Política Apple: Metal.

---

### Task 1–3: Implementado 2026-07-21

- [x] `rsx_metal_content_hold` / `force_bind_rgba` + present priority movie>hold>RT
- [x] `rsx_host_content.*` + call sites HOSTRES/WAD
- [x] movie begin pause / end re-arm opt-in; DEMO smoke multi-cor

---

## Aceite M8

| ID | Critério |
|----|----------|
| H1 | Hold mostra RGBA sem guest shaders |
| H2 | Movie HLE pausa hold (log `[CONTENT] pause`) |
| H3 | Win path intacto |

## Fora de escopo

Decode completo de todos os HOSTRES formats (só RGBA upload); UI scale.
