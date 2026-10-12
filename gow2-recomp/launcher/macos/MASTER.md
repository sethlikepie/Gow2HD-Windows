# GoW2 Recomp launcher — design system (MASTER)

Single source of truth for the launcher's visual layer. Every color, size,
radius and animation in the Swift code comes from `Theme.swift`, which mirrors
this file. Change a token here first, then in `Theme.swift`.

## Theses (validated 2026-09-22, revision 2 — "closer to God of War II")

**Visual.** The palette of the God of War II menus: charcoal-black stone base
with a subtle procedural stone texture, blood red as the primary color (primary
action, toggles, selection), aged gold for frames, icons and the Greek key
(meander) bands that border cards, headers and the sidebar selection;
parchment text. Titles in a Greek-feeling serif (New York), caps, wide
tracking, with a gold glow. Native macOS structure (sidebar, grouped forms,
SF Pro for body copy), 8 pt grid, 10–14 pt corners, no hard drop shadows.

**Interaction.** Embers rising over the Play hero (particles), the title's fire
glow breathing slowly, the Play button catching flame on hover, the sidebar
selection sliding between rows with its meander band, screens entering with a
fade and a short rise. Easing is ease-out / critically damped: no bounce, no
elastic, no layout-size animation. Reduce Motion turns off embers, glow
breathing, Ken Burns, flame flicker and the rise (fades stay).

## Color

| Token | Hex | Use |
|---|---|---|
| `stone950` | `#0E0D0C` | window background |
| `stone900` | `#171513` | surfaces (cards, form rows) |
| `stone800` | `#221F1C` | raised surface, hover fill |
| `stone700` | `#2E2A26` | hairline borders |
| `parchment` | `#EDE6DC` | primary text |
| `ash` | `#A89F93` | secondary text |
| `blood` | `#9B1B12` | primary: button, switches, selection fill (never as text on stone: ~2:1) |
| `bloodHi` | `#B8261A` | primary hover |
| `bloodLo` | `#6E120C` | primary pressed, gradient foot |
| `gold` | `#C9A15A` | frames, icons, meander, eyebrows; app tint for text-bearing controls |
| `goldHi` | `#E3C27E` | selected label, title glow core |
| `ember` | `#E0762C` | particles and glow only (never text) |
| `laurel` | `#7FA36B` | success / OK status |
| `amber` | `#D0A23C` | warning status |

Contrast (WCAG, computed): `parchment`/`stone950` 15.7:1, `ash`/`stone950`
7.4:1, `gold`/`stone950` 8.1:1, `goldHi`/`stone950` 11.4:1,
`parchment`/`blood` 6.6:1, `parchment`/`bloodHi` 5.1:1.

## Typography

| Token | Spec | Use |
|---|---|---|
| `display` | New York 44 pt bold, caps, tracking 6, gold glow | Play hero title |
| `title` | New York 24 pt semibold, caps, tracking 2 | screen titles |
| `headline` | SF Pro 15 pt semibold | row titles, patch names |
| `body` | SF Pro 13 pt regular | copy |
| `caption` | SF Pro 11 pt regular | paths, hints |
| `mono` | SF Mono 11 pt | hashes, log |
| `eyebrow` | SF Pro 11 pt semibold, caps, tracking 2, `gold` | section labels |

## Spacing, radius

Spacing `xs 4 · sm 8 · md 12 · lg 16 · xl 24 · xxl 32 · hero 48`.
Radius `sm 6 · md 10 · lg 14`. Meander band height `8`.

## Motion

| Token | Value |
|---|---|
| `hover` | easeOut 0.15 s |
| `press` | easeOut 0.10 s, scale 0.98 |
| `section` | easeOut 0.28 s: opacity 0 → 1, offset y 10 → 0 |
| `selection` | spring(response 0.30, damping 1.0) — slide, no overshoot |
| `kenBurns` | easeInOut 24 s, autoreverse, scale 1.00 → 1.06 |
| `glowBreath` | easeInOut 3.2 s, autoreverse, glow radius 6 → 14 |
| `embers` | 48 particles, rise 9–22 s per screen height, sway ±14 pt, fade out in the top 35 % |
| `flame` | hover glow radius 18, flicker ±25 % at ~7 Hz |

## Components

- **Primary button**: blood gradient (`bloodHi` → `blood`), gold hairline,
  parchment label; hover = flame glow; pressed = scale 0.98 + `bloodLo`.
- **Secondary button**: `stone800` fill, `stone700` hairline, `parchment` label.
- **Sidebar row**: gold icon; selected = `blood` 22 % fill, `goldHi` label and
  a meander band along the bottom, sliding between rows.
- **Screen header**: caps serif title with gold glow over a meander divider.
- **Status chip / badge**: capsule on `stone900`, icon in `laurel`/`blood`/`amber`.
