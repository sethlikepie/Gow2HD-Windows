#!/usr/bin/env python3
"""Setup (ELF + USRDIR) + mods + Jogar. Uma tela."""
from __future__ import annotations

import hashlib
import json
import os
import re
import shlex
import shutil
import sys
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
CONFIG_PATH = HERE / "user_config.json"
DEFAULT_MODS = HERE / "mods"

SAFE_NAME = re.compile(r"^[A-Za-z0-9._-]{1,64}$")


def default_config() -> dict:
    elf = HERE / "EBOOT.ELF"
    usr = HERE / "extracted" / "USRDIR"
    cache = HERE / "movie_cache"
    return {
        "elf": str(elf) if elf.is_file() else "",
        "vfs_root": str(usr) if usr.is_dir() else "",
        "movie_cache": str(cache) if cache.is_dir() else str(cache),
        "mods_dir": str(DEFAULT_MODS),
        "mods_enabled": [],
        # Runtime overlay settings file (F1/Start menu). Empty = the per-user
        # default the runtime also uses when started without the launcher.
        "overlay_settings": "",
    }


def default_overlay_settings_path() -> str:
    """Same location as rsx_overlay_settings_default_path() in the runtime."""
    if sys.platform == "win32":
        base = os.environ.get("APPDATA", "")
        return str(Path(base) / "ps3recomp" / "gow2" / "runtime-overlay.settings") if base else ""
    home = os.environ.get("HOME", "")
    if sys.platform == "darwin":
        if not home:
            return ""
        return f"{home}/Library/Application Support/ps3recomp/gow2/runtime-overlay.settings"
    xdg = os.environ.get("XDG_CONFIG_HOME", "")
    if xdg:
        return f"{xdg}/ps3recomp/gow2/runtime-overlay.settings"
    return f"{home}/.config/ps3recomp/gow2/runtime-overlay.settings" if home else ""


def overlay_settings_path(cfg: dict) -> str:
    """Resolved runtime-overlay settings path handed to the game."""
    raw = cfg.get("overlay_settings")
    if isinstance(raw, str) and raw.strip():
        return os.path.expanduser(raw.strip())
    return default_overlay_settings_path()


def load_config() -> dict:
    cfg = default_config()
    if CONFIG_PATH.is_file():
        try:
            data = json.loads(CONFIG_PATH.read_text())
            if isinstance(data, dict):
                cfg.update({k: data[k] for k in cfg if k in data})
        except (OSError, json.JSONDecodeError):
            pass
    return cfg


def save_config(cfg: dict) -> None:
    CONFIG_PATH.write_text(json.dumps(cfg, indent=2) + "\n")


def is_elf(path: str) -> bool:
    p = Path(path)
    if not p.is_file():
        return False
    try:
        with p.open("rb") as f:
            return f.read(4) == b"\x7fELF"
    except OSError:
        return False


def is_usrdir(path: str) -> bool:
    p = Path(path)
    if not p.is_dir():
        return False
    names = {n.name.lower() for n in p.iterdir()}
    if "gow2.psarc" in names or "usrdir" in names:
        return True
    return any(p.iterdir())


def setup_ok(cfg: dict) -> tuple[bool, str]:
    if not is_elf(cfg.get("elf", "")):
        return False, "Falta o EBOOT.ELF (dump teu do GoW2 HD)."
    vfs = cfg.get("vfs_root", "")
    if not is_usrdir(vfs):
        return False, "Falta a pasta extracted/USRDIR."
    return True, "Pronto para jogar."


def list_mod_names(mods_dir: str) -> list[str]:
    p = Path(mods_dir)
    if not p.is_dir():
        return []
    names = []
    for child in sorted(p.iterdir(), key=lambda x: x.name.lower()):
        if child.is_dir() and not child.name.startswith(".") and SAFE_NAME.match(child.name):
            names.append(child.name)
    return names


def safe_mod_name(raw: str) -> str:
    stem = re.sub(r"[^A-Za-z0-9._-]+", "_", raw).strip("._-")
    return stem[:64] or "mod"


def unique_dest(mods_dir: Path, name: str) -> Path:
    dest = mods_dir / name
    n = 2
    while dest.exists():
        dest = mods_dir / f"{name}_{n}"
        n += 1
    return dest


def zip_payload_root(extracted: Path) -> Path:
    cur = extracted
    for _ in range(2):
        kids = [
            p
            for p in cur.iterdir()
            if p.name not in (".", "..", "__MACOSX") and not p.name.startswith(".")
        ]
        files = [p for p in kids if p.is_file()]
        dirs = [p for p in kids if p.is_dir()]
        if files or len(dirs) != 1:
            return cur
        if dirs[0].name in ("USRDIR", "movie_cache", "extracted"):
            return cur
        cur = dirs[0]
    return cur


def extract_zip_safe(zip_path: Path, dest: Path) -> None:
    dest = dest.resolve()
    dest.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(zip_path) as zf:
        for info in zf.infolist():
            target = (dest / info.filename).resolve()
            if dest != target and not str(target).startswith(str(dest) + os.sep):
                raise ValueError(f"zip inseguro: {info.filename}")
        zf.extractall(dest)


def import_zip(zip_path: str, mods_dir: str) -> str:
    src = Path(zip_path)
    if not src.is_file():
        raise FileNotFoundError(zip_path)
    root = Path(mods_dir)
    root.mkdir(parents=True, exist_ok=True)
    name = safe_mod_name(src.stem)
    dest = unique_dest(root, name)
    tmp = dest.with_name(dest.name + ".__unpack")
    if tmp.exists():
        shutil.rmtree(tmp)
    extract_zip_safe(src, tmp)
    payload = zip_payload_root(tmp)
    if payload == tmp:
        tmp.rename(dest)
    else:
        shutil.copytree(payload, dest)
        shutil.rmtree(tmp, ignore_errors=True)
    mac = dest / "__MACOSX"
    if mac.exists():
        shutil.rmtree(mac, ignore_errors=True)
    return dest.name


def import_folder(folder: str, mods_dir: str) -> str:
    src = Path(folder)
    if not src.is_dir():
        raise FileNotFoundError(folder)
    root = Path(mods_dir)
    root.mkdir(parents=True, exist_ok=True)
    name = safe_mod_name(src.name)
    dest = unique_dest(root, name)
    shutil.copytree(src, dest)
    return dest.name


def find_binary() -> Path | None:
    for name in ("boot_gow2_mods", "g2play", "boot_gow2"):
        p = HERE / name
        if p.is_file() and os.access(p, os.X_OK):
            return p
    return None


def savedata_root_of(cfg: dict) -> Path:
    raw = str(cfg.get("savedata_root") or os.environ.get("PS3_SAVEDATA_ROOT") or "").strip()
    if raw:
        return Path(raw)
    if sys.platform == "darwin":
        base = Path.home() / "Library" / "Application Support" / "ps3recomp"
    elif os.name == "nt":
        base = Path(os.environ.get("APPDATA") or Path.home()) / "ps3recomp"
    else:
        base = Path.home() / ".local" / "share" / "ps3recomp"
    return base / "dev_hdd0" / "home" / "00000001" / "savedata"


def _relative_save_file(path: str) -> bool:
    if not path or path.startswith(("/", "\\")) or "\\" in path:
        return False
    parts = path.split("/")
    return all(part and part not in (".", "..") for part in parts)


def _autosave_slot_valid(slot: Path) -> bool:
    if slot.is_symlink() or not slot.is_dir():
        return False
    current = slot / "current"
    data = current / "data"
    try:
        manifest = json.loads((current / "manifest.json").read_text())
    except (OSError, json.JSONDecodeError, UnicodeError):
        return False
    if not isinstance(manifest, dict) or manifest.get("version") != 1:
        return False
    title = manifest.get("title")
    source = manifest.get("source")
    files = manifest.get("files")
    if not isinstance(title, str) or not SAFE_NAME.match(title):
        return False
    if not isinstance(source, str) or not SAFE_NAME.match(source):
        return False
    if not isinstance(files, list) or not files:
        return False
    try:
        data_root = data.resolve()
    except OSError:
        return False
    seen: set[str] = set()
    for item in files:
        if not isinstance(item, dict):
            return False
        rel = item.get("path")
        size = item.get("size")
        digest = item.get("sha256")
        if not isinstance(rel, str) or rel in seen or not _relative_save_file(rel):
            return False
        if isinstance(size, bool) or not isinstance(size, int) or size < 0:
            return False
        if not isinstance(digest, str) or len(digest) != 64:
            return False
        if any(c not in "0123456789abcdef" for c in digest):
            return False
        seen.add(rel)
        try:
            file_path = (data / rel).resolve()
            blob = file_path.read_bytes()
        except OSError:
            return False
        if data_root != file_path and not str(file_path).startswith(str(data_root) + os.sep):
            return False
        if len(blob) != size or hashlib.sha256(blob).hexdigest() != digest:
            return False
    return True


def autosave_available(cfg: dict) -> bool:
    """True when one title slot under the save root has a complete manifest."""
    root = savedata_root_of(cfg)
    parent = root.parent
    if parent == root:
        return False
    base = parent / "ps3recomp-autosave"
    if base.is_symlink() or not base.is_dir():
        return False
    try:
        children = list(base.iterdir())
    except OSError:
        return False
    for child in children:
        if child.name.startswith("."):
            continue
        if _autosave_slot_valid(child):
            return True
    return False


def build_launch_script(cfg: dict, resume_autosave: bool = False,
                        binary: Path | None = None) -> str:
    binary = binary or find_binary() or (HERE / "boot_gow2")
    env_sh = HERE / "env_gow2.sh"
    mods_dir = cfg.get("mods_dir") or str(DEFAULT_MODS)
    enabled = [n for n in cfg.get("mods_enabled") or [] if n in list_mod_names(mods_dir)]
    cache = cfg.get("movie_cache") or str(HERE / "movie_cache")
    # PS3_OVERLAY_SETTINGS from the caller's environment wins, like the other
    # overrides below; otherwise the configured (or per-user default) path.
    overlay = os.environ.get("PS3_OVERLAY_SETTINGS") or overlay_settings_path(cfg)
    overlay_line = f"export PS3_OVERLAY_SETTINGS={shlex.quote(overlay)}" if overlay else ""
    if resume_autosave:
        # The original menu's AutoLoad reads the mirror. Leave the pad idle so
        # autostart does not confirm New Game over that load.
        autosave_env = (
            "export PS3_AUTOSAVE_RESUME=1\n"
            "export PS3_AUTOSAVE_MENU=1\n"
            "export PS3_PAD_AUTOSTART=0\n"
        )
    else:
        autosave_env = (
            "unset PS3_AUTOSAVE_RESUME\n"
            "export PS3_PAD_AUTOSTART=\"${PS3_PAD_AUTOSTART:-1}\"\n"
        )
    return f"""
set -euo pipefail
cd {HERE.as_posix()!r}
G2_MUTE="${{PS3_MUTE-}}"
G2_DONE="${{PS3_MOVIE_DONE_MS-}}"
G2_FULLSCREEN="${{PS3_FULLSCREEN-}}"
G2_VSYNC="${{PS3_METAL_VSYNC-}}"
set -a
. {env_sh.as_posix()!r}
set +a
export PS3_VFS_ROOT={cfg.get('vfs_root', '')!r}
export PS3_MOVIE_CACHE={cache!r}
export PS3_MODS_DIR={mods_dir!r}
export PS3_MODS_ENABLED={','.join(enabled)!r}
export PS3_MUTE="${{G2_MUTE:-0}}"
export PS3_MOVIE_DONE_MS="${{G2_DONE:-auto}}"
export PS3_METALFX="${{PS3_METALFX:-1}}"
export PS3_METAL_PASS_MERGE="${{PS3_METAL_PASS_MERGE:-1}}"
export PS3_METAL_GPU_DESWIZZLE="${{PS3_METAL_GPU_DESWIZZLE:-1}}"
export PS3_METAL_HDR="${{PS3_METAL_HDR:-0}}"
{overlay_line}
# Fullscreen and VSync come from the overlay settings file (defaults: window,
# VSync on). An explicit PS3_FULLSCREEN / PS3_METAL_VSYNC from the caller still
# wins; env_gow2.sh's own VSync default must not mask the persisted choice.
if [ -n "$G2_FULLSCREEN" ]; then export PS3_FULLSCREEN="$G2_FULLSCREEN"; else unset PS3_FULLSCREEN; fi
if [ -n "$G2_VSYNC" ]; then export PS3_METAL_VSYNC="$G2_VSYNC"; else unset PS3_METAL_VSYNC; fi
{autosave_env}unset PS3_NO_RSX
exec {binary.as_posix()!r} {cfg.get('elf', '')!r}
"""


def play(cfg: dict, resume_autosave: bool = False) -> int:
    ok, msg = setup_ok(cfg)
    if not ok:
        print(msg, file=sys.stderr)
        return 2
    if resume_autosave and not autosave_available(cfg):
        print("Nenhum autosave válido para continuar.", file=sys.stderr)
        return 2
    binary = find_binary()
    if binary is None:
        print("Falta boot_gow2. Compila com ./build_macos.sh recomp_macos_e435", file=sys.stderr)
        return 2
    mods_dir = cfg.get("mods_dir") or str(DEFAULT_MODS)
    Path(mods_dir).mkdir(parents=True, exist_ok=True)
    script = build_launch_script(cfg, resume_autosave=resume_autosave, binary=binary)
    os.execvp("/bin/bash", ["/bin/bash", "-lc", script])
    return 127


def status_dict(cfg: dict) -> dict:
    """Machine-readable state for the native macOS launcher (--status-json)."""
    ok, msg = setup_ok(cfg)
    binary = find_binary()
    mods_dir = cfg.get("mods_dir") or str(DEFAULT_MODS)
    names = list_mod_names(mods_dir)
    return {
        "setup_ok": ok,
        "message": msg,
        "elf": cfg.get("elf", ""),
        "elf_ok": is_elf(cfg.get("elf", "")),
        "vfs_root": cfg.get("vfs_root", ""),
        "vfs_ok": is_usrdir(cfg.get("vfs_root", "")),
        "movie_cache": cfg.get("movie_cache", ""),
        "movie_cache_ok": Path(cfg.get("movie_cache") or "").is_dir(),
        "binary": str(binary) if binary else "",
        "autosave": autosave_available(cfg),
        "savedata_root": str(savedata_root_of(cfg)),
        "mods_dir": mods_dir,
        "mods": [{"name": n, "enabled": n in (cfg.get("mods_enabled") or [])} for n in names],
    }


def self_test() -> int:
    import tempfile

    fails = 0

    def check(cond: bool, label: str) -> None:
        nonlocal fails
        if not cond:
            print("FALHOU:", label)
            fails += 1

    with tempfile.TemporaryDirectory() as td:
        t = Path(td)
        zpath = t / "pack.zip"
        inner = t / "build"
        (inner / "nested" / "USRDIR").mkdir(parents=True)
        (inner / "nested" / "USRDIR" / "foo.wad").write_text("B")
        with zipfile.ZipFile(zpath, "w") as zf:
            zf.write(inner / "nested" / "USRDIR" / "foo.wad", "nested/USRDIR/foo.wad")
        name = import_zip(str(zpath), str(t / "mods"))
        got = t / "mods" / name / "USRDIR" / "foo.wad"
        check(got.is_file() and got.read_text() == "B", "zip flatten USRDIR")

        evil = t / "evil.zip"
        with zipfile.ZipFile(evil, "w") as zf:
            zf.writestr("../outside.txt", "nope")
        threw = False
        try:
            import_zip(str(evil), str(t / "mods"))
        except ValueError:
            threw = True
        check(threw, "zip slip recusado")

        folder = t / "pasta_mod"
        folder.mkdir()
        (folder / "bar.txt").write_text("ok")
        n2 = import_folder(str(folder), str(t / "mods"))
        check((t / "mods" / n2 / "bar.txt").read_text() == "ok", "import pasta")

        save_root = t / "savedata"
        save_root.mkdir()
        cfg = {"savedata_root": str(save_root), "elf": "EBOOT.ELF", "vfs_root": "USRDIR"}
        check(not autosave_available(cfg), "sem autosave")
        payload = b"PARAM"
        slot = save_root.parent / "ps3recomp-autosave" / "NPUA80491" / "current"
        (slot / "data").mkdir(parents=True)
        (slot / "data" / "PARAM.SFO").write_bytes(payload)
        (slot / "manifest.json").write_text(json.dumps({
            "version": 1,
            "title": "NPUA80491",
            "source": "BCUS98229_GOW2",
            "captured_at": 1,
            "files": [{
                "path": "PARAM.SFO",
                "size": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }],
        }))
        check(autosave_available(cfg), "autosave válido disponível")
        check("PS3_AUTOSAVE_RESUME=1" in build_launch_script(cfg, resume_autosave=True),
              "continuar exporta modo")
        check("PS3_AUTOSAVE_RESUME=1" not in build_launch_script(cfg, resume_autosave=False),
              "jogar normal não exporta modo")
        (slot / "data" / "PARAM.SFO").write_bytes(b"PARAX")
        check(not autosave_available(cfg), "digest ruim recusado")

    check(is_elf("/no/such") is False, "ELF ausente")
    st = status_dict({"elf": "/no/such", "vfs_root": "/no/such", "mods_dir": "/no/such"})
    check(st["setup_ok"] is False and st["elf_ok"] is False and st["mods"] == [],
          "status-json sem arquivos")
    check(json.loads(json.dumps(st))["message"] == st["message"], "status-json serializa")
    print("gow2_launcher self-test:", "FALHOU" if fails else "PASS")
    return 1 if fails else 0


def run_ui(cfg: dict) -> int:
    try:
        import tkinter as tk
        from tkinter import filedialog, messagebox
    except ImportError:
        print("Tkinter ausente. Comandos:")
        print("  python3 gow2_launcher.py --elf CAMINHO --usrdir PASTA --save")
        print("  python3 gow2_launcher.py --import-zip arquivo.zip")
        print("  python3 gow2_launcher.py --play")
        return 2

    BG, FG, CARD, GOLD, MUTED = "#161210", "#F3E6D4", "#231C18", "#C9A227", "#A09080"

    root = tk.Tk()
    root.title("God of War II HD")
    root.configure(bg=BG)
    root.minsize(640, 520)
    root.geometry("720x560")

    elf_var = tk.StringVar(value=cfg.get("elf", ""))
    vfs_var = tk.StringVar(value=cfg.get("vfs_root", ""))
    status_var = tk.StringVar()
    mod_vars: dict[str, tk.BooleanVar] = {}

    def snapshot() -> dict:
        out = dict(cfg)
        out["elf"] = elf_var.get().strip()
        out["vfs_root"] = vfs_var.get().strip()
        out["mods_dir"] = out.get("mods_dir") or str(DEFAULT_MODS)
        out["mods_enabled"] = [n for n, v in mod_vars.items() if v.get()]
        if not out.get("movie_cache"):
            out["movie_cache"] = str(HERE / "movie_cache")
        return out

    def refresh_status() -> None:
        data = snapshot()
        ok, msg = setup_ok(data)
        have_save = ok and autosave_available(data)
        status_var.set(msg if not have_save else msg + " Autosave pronto no menu do jogo.")
        continue_btn.pack_forget()
        jogar_btn.pack_forget()
        if have_save:
            continue_btn.configure(state="normal")
            continue_btn.pack(fill="x", padx=18, pady=(8, 4))
            jogar_btn.configure(text="JOGAR NORMALMENTE", state="normal")
        else:
            continue_btn.configure(state="disabled")
            jogar_btn.configure(text="JOGAR", state=("normal" if ok else "disabled"))
        jogar_btn.pack(fill="x", padx=18, pady=(4, 18))

    def persist() -> dict:
        data = snapshot()
        save_config(data)
        cfg.clear()
        cfg.update(data)
        refresh_status()
        return data

    def pick_elf() -> None:
        path = filedialog.askopenfilename(
            title="EBOOT.ELF do GoW2 HD",
            initialdir=str(HERE),
            filetypes=[("ELF", "*.ELF *.elf"), ("Todos", "*")],
        )
        if path:
            elf_var.set(path)
            persist()

    def pick_usrdir() -> None:
        path = filedialog.askdirectory(title="Pasta extracted/USRDIR", initialdir=str(HERE))
        if path:
            p = Path(path)
            if p.name != "USRDIR" and (p / "USRDIR").is_dir():
                path = str(p / "USRDIR")
            vfs_var.set(path)
            persist()

    def rebuild_mods() -> None:
        for child in mods_list.winfo_children():
            child.destroy()
        mod_vars.clear()
        data = snapshot()
        Path(data["mods_dir"]).mkdir(parents=True, exist_ok=True)
        enabled = set(data.get("mods_enabled") or [])
        names = list_mod_names(data["mods_dir"])
        if not names:
            tk.Label(
                mods_list,
                text="Nenhum mod. Importa um zip ou uma pasta.",
                bg=CARD,
                fg=MUTED,
                font=("Helvetica", 13),
            ).pack(anchor="w", padx=8, pady=8)
            return
        for name in names:
            var = tk.BooleanVar(value=name in enabled)
            mod_vars[name] = var
            row = tk.Frame(mods_list, bg=CARD)
            row.pack(fill="x", padx=6, pady=3)
            tk.Checkbutton(
                row,
                text=name,
                variable=var,
                command=persist,
                bg=CARD,
                fg=FG,
                selectcolor=BG,
                activebackground=CARD,
                activeforeground=GOLD,
                font=("Helvetica", 14),
                highlightthickness=0,
            ).pack(side="left")

    def do_import_zip() -> None:
        path = filedialog.askopenfilename(
            title="Zip do mod",
            filetypes=[("Zip", "*.zip"), ("Todos", "*")],
        )
        if not path:
            return
        try:
            name = import_zip(path, snapshot()["mods_dir"])
        except (OSError, ValueError, zipfile.BadZipFile) as e:
            messagebox.showerror("Importar zip", str(e))
            return
        data = persist()
        enabled = list(data.get("mods_enabled") or [])
        if name not in enabled:
            enabled.append(name)
        data["mods_enabled"] = enabled
        save_config(data)
        cfg.update(data)
        rebuild_mods()
        persist()

    def do_import_folder() -> None:
        path = filedialog.askdirectory(title="Pasta do mod")
        if not path:
            return
        try:
            name = import_folder(path, snapshot()["mods_dir"])
        except OSError as e:
            messagebox.showerror("Importar pasta", str(e))
            return
        data = persist()
        enabled = list(data.get("mods_enabled") or [])
        if name not in enabled:
            enabled.append(name)
        data["mods_enabled"] = enabled
        save_config(data)
        cfg.update(data)
        rebuild_mods()
        persist()

    def do_delete() -> None:
        chosen = [n for n, v in mod_vars.items() if v.get()]
        if not chosen:
            messagebox.showinfo("Apagar", "Marca o mod que queres apagar.")
            return
        if not messagebox.askyesno("Apagar", "Apagar: " + ", ".join(chosen) + " ?"):
            return
        data = snapshot()
        root = Path(data["mods_dir"])
        for name in chosen:
            shutil.rmtree(root / name, ignore_errors=True)
        data["mods_enabled"] = [n for n in (data.get("mods_enabled") or []) if n not in chosen]
        save_config(data)
        cfg.update(data)
        rebuild_mods()
        persist()

    def do_play(resume_autosave: bool = False) -> None:
        data = persist()
        root.destroy()
        raise SystemExit(play(data, resume_autosave=resume_autosave))

    pad = {"padx": 18, "pady": 6}
    tk.Label(
        root,
        text="God of War II HD",
        bg=BG,
        fg=GOLD,
        font=("Helvetica", 22, "bold"),
    ).pack(anchor="w", padx=18, pady=(16, 4))
    tk.Label(
        root,
        text="O ELF é o dump teu do jogo. Mods substituem arquivos em USRDIR / movie_cache.",
        bg=BG,
        fg=MUTED,
        font=("Helvetica", 12),
        wraplength=680,
        justify="left",
    ).pack(anchor="w", padx=18, pady=(0, 10))

    def path_row(parent, label, var, browse) -> None:
        box = tk.Frame(parent, bg=CARD)
        box.pack(fill="x", padx=18, pady=4)
        tk.Label(box, text=label, bg=CARD, fg=MUTED, font=("Helvetica", 11)).pack(
            anchor="w", padx=10, pady=(8, 0)
        )
        line = tk.Frame(box, bg=CARD)
        line.pack(fill="x", padx=10, pady=(2, 10))
        tk.Entry(
            line,
            textvariable=var,
            bg="#2C241F",
            fg=FG,
            insertbackground=FG,
            relief="flat",
            font=("Menlo", 12),
        ).pack(side="left", fill="x", expand=True, ipady=6)
        tk.Button(
            line,
            text="Escolher",
            command=browse,
            bg=GOLD,
            fg="#1A140F",
            relief="flat",
            font=("Helvetica", 12, "bold"),
            padx=12,
        ).pack(side="left", padx=(8, 0))

    path_row(root, "EBOOT.ELF", elf_var, pick_elf)
    path_row(root, "Pasta USRDIR", vfs_var, pick_usrdir)

    tk.Label(root, textvariable=status_var, bg=BG, fg=GOLD, font=("Helvetica", 13)).pack(
        anchor="w", **pad
    )

    mods_card = tk.Frame(root, bg=CARD)
    mods_card.pack(fill="both", expand=True, padx=18, pady=6)
    tk.Label(
        mods_card, text="Mods", bg=CARD, fg=FG, font=("Helvetica", 16, "bold")
    ).pack(anchor="w", padx=10, pady=(10, 4))
    mods_list = tk.Frame(mods_card, bg=CARD)
    mods_list.pack(fill="both", expand=True)
    btns = tk.Frame(mods_card, bg=CARD)
    btns.pack(fill="x", padx=10, pady=(4, 10))
    for text, cmd in (
        ("Importar zip", do_import_zip),
        ("Importar pasta", do_import_folder),
        ("Apagar marcados", do_delete),
    ):
        tk.Button(
            btns,
            text=text,
            command=cmd,
            bg="#3A2F28",
            fg=FG,
            relief="flat",
            font=("Helvetica", 12),
            padx=10,
            pady=6,
        ).pack(side="left", padx=(0, 8))

    continue_btn = tk.Button(
        root,
        text="CONTINUAR AUTOSAVE",
        command=lambda: do_play(True),
        bg=GOLD,
        fg="#1A140F",
        relief="flat",
        font=("Helvetica", 18, "bold"),
        pady=10,
    )
    jogar_btn = tk.Button(
        root,
        text="JOGAR",
        command=lambda: do_play(False),
        bg="#3A2F28",
        fg=FG,
        relief="flat",
        font=("Helvetica", 16, "bold"),
        pady=8,
    )

    elf_var.trace_add("write", lambda *_: refresh_status())
    vfs_var.trace_add("write", lambda *_: refresh_status())
    rebuild_mods()
    refresh_status()
    root.mainloop()
    persist()
    return 0


def main(argv: list[str]) -> int:
    cfg = load_config()
    args = argv[1:]
    if "--self-test" in args:
        return self_test()
    if "--continue" in args:
        return play(cfg, resume_autosave=True)
    if "--play" in args:
        return play(cfg)
    if "--status-json" in args:
        print(json.dumps(status_dict(cfg)))
        return 0
    if "--play-or-ui" in args:
        ok, _ = setup_ok(cfg)
        if ok and find_binary() is not None:
            return play(cfg)
        return run_ui(cfg)

    i = 0
    save = False
    while i < len(args):
        a = args[i]
        if a == "--elf" and i + 1 < len(args):
            cfg["elf"] = args[i + 1]
            i += 2
            save = True
            continue
        if a == "--usrdir" and i + 1 < len(args):
            cfg["vfs_root"] = args[i + 1]
            i += 2
            save = True
            continue
        if a == "--movie-cache" and i + 1 < len(args):
            cfg["movie_cache"] = args[i + 1]
            i += 2
            save = True
            continue
        if a == "--mods-enabled" and i + 1 < len(args):
            names = set(list_mod_names(cfg.get("mods_dir") or str(DEFAULT_MODS)))
            cfg["mods_enabled"] = [n for n in args[i + 1].split(",") if n in names]
            i += 2
            save = True
            continue
        if a == "--import-zip" and i + 1 < len(args):
            name = import_zip(args[i + 1], cfg.get("mods_dir") or str(DEFAULT_MODS))
            enabled = list(cfg.get("mods_enabled") or [])
            if name not in enabled:
                enabled.append(name)
            cfg["mods_enabled"] = enabled
            save = True
            i += 2
            continue
        if a == "--save":
            save = True
            i += 1
            continue
        print("uso: gow2_launcher.py [--play | --continue | --play-or-ui | --self-test]")
        print("     [--status-json] [--elf PATH --usrdir PATH --movie-cache PATH --save]")
        print("     [--import-zip ZIP] [--mods-enabled a,b]")
        return 2
    if save:
        save_config(cfg)
        ok, msg = setup_ok(cfg)
        print(msg)
        return 0 if ok else 2
    return run_ui(cfg)


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
