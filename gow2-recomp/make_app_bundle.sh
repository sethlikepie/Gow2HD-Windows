#!/usr/bin/env bash
# Gera "God of War II HD.app" -- um bundle macOS que apresenta o binario
# recompilado como o jogo real: nome, icone (Finder/Dock/barra de menus) e
# duplo-clique, tudo a partir dos DADOS DO PROPRIO JOGO (PARAM.SFO/ICON0.PNG).
#
# O .app NAO e' versionado (contem hard-link do boot_gow2 + icone derivado do
# jogo). Re-correr apos cada ./build_macos.sh (o hard-link aponta para o binario
# de entao). Uso: ./make_app_bundle.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

[ -x ./boot_gow2 ] || { echo "boot_gow2 nao existe -- rode ./build_macos.sh primeiro" >&2; exit 1; }
SFO="extracted/PARAM.SFO"; ICON="extracted/ICON0.PNG"; PIC1="extracted/PIC1.PNG"
[ -f "$SFO" ]  || { echo "$SFO nao encontrado" >&2; exit 1; }
[ -f "$ICON" ] || { echo "$ICON nao encontrado" >&2; exit 1; }
[ -f "$PIC1" ] || PIC1=""   # sem hero art -> fallback (logo em tile escuro)

# --- 1. Metadados do jogo (TITLE + APP_VER) + icone master 1024, do jogo ------
# Icone segue a grelha do macOS: corpo 824 em canvas 1024 (margem 100),
# squircle (raio 185) + sombra suave, arte do Kratos (PIC1) a preencher e o
# logo GOD OF WAR II (ICON0) sobre um scrim -- look "padrao de jogo".
WORK="$(mktemp -d)"; trap 'rm -rf "$WORK"' EXIT
python3 - "$SFO" "$ICON" "$PIC1" "$WORK/master.png" "$WORK/meta.txt" <<'PY'
import struct, sys, os
from PIL import Image, ImageDraw, ImageFilter
sfo, icon0, pic1, master_out, meta_out = sys.argv[1:6]

# PARAM.SFO -> TITLE, APP_VER
d = open(sfo, 'rb').read()
_, _, ks, ds, n = struct.unpack_from('<IIIII', d, 0)
title, ver = 'God of War II HD', '1.00'
for i in range(n):
    ko, fmt, dl, dm, do = struct.unpack_from('<HHIII', d, 0x14 + i*16)
    k = d[ks+ko:d.index(b'\0', ks+ko)].decode('ascii', 'replace')
    v = d[ds+do:ds+do+dl].split(b'\0')[0].decode('utf-8', 'replace')
    if k == 'TITLE':   title = v
    elif k == 'APP_VER': ver = v
open(meta_out, 'w').write(title + '\n' + ver + '\n')

S, BODY, RAD = 1024, 824, 185
MARG = (S - BODY) // 2
def squircle(sz, r):
    m = Image.new('L', (sz, sz), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, sz-1, sz-1], radius=r, fill=255)
    return m
def csquare(im):
    w, h = im.size; s = min(w, h)
    return im.crop(((w-s)//2, (h-s)//2, (w-s)//2+s, (h-s)//2+s))
mask = squircle(BODY, RAD)

if pic1 and os.path.isfile(pic1):
    art = csquare(Image.open(pic1).convert('RGB')).resize((BODY, BODY), Image.LANCZOS).convert('RGBA')
else:
    art = Image.new('RGBA', (BODY, BODY), (18, 18, 26, 255))

body = Image.new('RGBA', (BODY, BODY), (0, 0, 0, 0))
body.paste(art, (0, 0)); body.putalpha(mask)
# scrim inferior para o logo assentar
scr = Image.new('L', (BODY, BODY), 0); sd = ImageDraw.Draw(scr)
for y in range(BODY):
    a = 0 if y < BODY*0.5 else int(210*((y-BODY*0.5)/(BODY*0.5))**1.4)
    sd.line([(0, y), (BODY, y)], fill=min(230, a))
scrl = Image.composite(Image.new('RGBA', (BODY, BODY), (8, 6, 10, 255)),
                       Image.new('RGBA', (BODY, BODY), (0, 0, 0, 0)), scr)
scrl.putalpha(Image.composite(scr, Image.new('L', (BODY, BODY), 0), mask))
body = Image.alpha_composite(body, scrl)
# logo do jogo (ICON0) em baixo
logo = Image.open(icon0).convert('RGBA')
lw = int(BODY*0.72); lh = max(1, int(logo.height*lw/logo.width))
body.paste(logo.resize((lw, lh), Image.LANCZOS), ((BODY-lw)//2, int(BODY*0.70)),
           logo.resize((lw, lh), Image.LANCZOS))

canvas = Image.new('RGBA', (S, S), (0, 0, 0, 0))
sil = Image.new('RGBA', (S, S), (0, 0, 0, 0)); sil.paste((0, 0, 0, 150), (MARG, MARG+14), mask)
canvas = Image.alpha_composite(canvas, sil.filter(ImageFilter.GaussianBlur(22)))
bl = Image.new('RGBA', (S, S), (0, 0, 0, 0)); bl.paste(body, (MARG, MARG), body)
canvas = Image.alpha_composite(canvas, bl)
# highlight superior subtil
grad = Image.new('L', (1, BODY))
for y in range(BODY): grad.putpixel((0, y), max(0, 40 - int(40*y/220)))
white = Image.new('RGBA', (BODY, BODY), (255, 255, 255, 255)); white.putalpha(grad.resize((BODY, BODY)))
wm = Image.composite(white, Image.new('RGBA', (BODY, BODY), (0, 0, 0, 0)), mask)
hl = Image.new('RGBA', (S, S), (0, 0, 0, 0)); hl.paste(wm, (MARG, MARG), wm)
canvas = Image.alpha_composite(canvas, hl)
canvas.save(master_out)
PY

TITLE="$(sed -n '1p' "$WORK/meta.txt")"
APPVER="$(sed -n '2p' "$WORK/meta.txt")"
# Nome de ficheiro sem simbolos de marca.
FNAME="$(printf '%s' "$TITLE" | sed 's/[®™©]//g; s/  */ /g; s/^ *//; s/ *$//')"
APP="$HERE/${FNAME}.app"
echo "[bundle] TITLE=\"$TITLE\"  APP_VER=$APPVER  ->  ${FNAME}.app"

# --- 2. .icns a partir do master (iconutil) -----------------------------------
ISET="$WORK/AppIcon.iconset"; mkdir -p "$ISET"
gen() { sips -z "$2" "$2" "$WORK/master.png" --out "$ISET/$1" >/dev/null; }
gen icon_16x16.png 16;      gen icon_16x16@2x.png 32
gen icon_32x32.png 32;      gen icon_32x32@2x.png 64
gen icon_128x128.png 128;   gen icon_128x128@2x.png 256
gen icon_256x256.png 256;   gen icon_256x256@2x.png 512
gen icon_512x512.png 512;   gen icon_512x512@2x.png 1024
iconutil -c icns "$ISET" -o "$WORK/AppIcon.icns"

# --- 3. Estrutura do bundle ---------------------------------------------------
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$WORK/AppIcon.icns" "$APP/Contents/Resources/AppIcon.icns"

# boot_gow2 DENTRO do bundle (hard-link, sem custo de disco) -> [NSBundle
# mainBundle] resolve para o .app e a barra de menus mostra CFBundleName.
ln -f "$HERE/boot_gow2" "$APP/Contents/MacOS/boot_gow2"

# Launcher = CFBundleExecutable: prepara env do jogo e faz exec do boot dentro
# do bundle. GAME_DIR e' embutido (onde vivem EBOOT.ELF/extracted/env_gow2.sh).
cat > "$APP/Contents/MacOS/launch" <<LAUNCH
#!/bin/bash
BUNDLE_MACOS="\$(cd "\$(dirname "\$0")" && pwd)"
GAME_DIR="$HERE"
cd "\$GAME_DIR" || exit 1
# Sem ELF configurado: abre a tela de setup/mods. Com ELF: joga.
if [ -f "\$GAME_DIR/gow2_launcher.py" ]; then
  export PS3_FULLSCREEN="\${PS3_FULLSCREEN:-1}"
  exec python3 "\$GAME_DIR/gow2_launcher.py" --play-or-ui
fi
. "\$GAME_DIR/env_gow2.sh"
export PS3_FULLSCREEN="\${PS3_FULLSCREEN:-1}"
exec "\$BUNDLE_MACOS/boot_gow2" "\$GAME_DIR/EBOOT.ELF"
LAUNCH
chmod +x "$APP/Contents/MacOS/launch"

# Info.plist (nome/icone/versao do jogo; MetalFX pede macOS 13+).
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>${FNAME}</string>
    <key>CFBundleDisplayName</key><string>${TITLE}</string>
    <key>CFBundleExecutable</key><string>launch</string>
    <key>CFBundleIdentifier</key><string>com.ps3recomp.npua80491</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>${APPVER}</string>
    <key>CFBundleVersion</key><string>${APPVER}</string>
    <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>NSHighResolutionCapable</key><true/>
    <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
</dict>
</plist>
PLIST

plutil -lint "$APP/Contents/Info.plist" >/dev/null && echo "[bundle] Info.plist ok"
touch "$APP"   # Finder refresca o icone
echo "[bundle] pronto: $APP"
echo "[bundle] abrir:  open \"$APP\"   (ou duplo-clique no Finder)"
