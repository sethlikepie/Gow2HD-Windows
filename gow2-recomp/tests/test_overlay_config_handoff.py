#!/usr/bin/env python3
"""Launcher -> runtime handoff of the overlay settings path (Task 6).

Run: python3 games/gow2/tests/test_overlay_config_handoff.py
"""
from __future__ import annotations

import importlib.util
import json
import os
import shlex
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

HERE = Path(__file__).resolve().parent
LAUNCHER = HERE.parent / "gow2_launcher.py"


def load_launcher():
    spec = importlib.util.spec_from_file_location("gow2_launcher_under_test", LAUNCHER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class OverlayConfigHandoff(unittest.TestCase):
    def setUp(self):
        self.launcher = load_launcher()
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.config = self.root / "user_config.json"
        self.launcher.CONFIG_PATH = self.config
        self.env = mock.patch.dict(os.environ, {"HOME": str(self.root / "home")}, clear=False)
        self.env.start()
        os.environ.pop("PS3_OVERLAY_SETTINGS", None)
        os.environ.pop("XDG_CONFIG_HOME", None)

    def tearDown(self):
        self.env.stop()
        self.tmp.cleanup()

    def default_path(self) -> str:
        home = str(self.root / "home")
        if sys.platform == "darwin":
            return f"{home}/Library/Application Support/ps3recomp/gow2/runtime-overlay.settings"
        return f"{home}/.config/ps3recomp/gow2/runtime-overlay.settings"

    def test_old_setup_json_without_overlay_key_is_compatible(self):
        self.config.write_text(json.dumps({
            "elf": "/games/EBOOT.ELF",
            "vfs_root": "/games/USRDIR",
            "mods_enabled": ["a"],
        }))
        cfg = self.launcher.load_config()
        self.assertEqual(cfg["elf"], "/games/EBOOT.ELF")
        self.assertEqual(cfg["vfs_root"], "/games/USRDIR")
        self.assertEqual(cfg["mods_enabled"], ["a"])
        self.assertEqual(cfg["overlay_settings"], "")
        self.assertEqual(self.launcher.overlay_settings_path(cfg), self.default_path())

    def test_malformed_setup_json_falls_back_to_defaults(self):
        self.config.write_text("{ not json")
        cfg = self.launcher.load_config()
        self.assertEqual(cfg["overlay_settings"], "")
        self.assertEqual(self.launcher.overlay_settings_path(cfg), self.default_path())
        self.config.write_text(json.dumps({"overlay_settings": 42}))
        cfg = self.launcher.load_config()
        self.assertEqual(self.launcher.overlay_settings_path(cfg), self.default_path())

    def test_configured_path_round_trips_and_expands_home(self):
        cfg = self.launcher.load_config()
        cfg["overlay_settings"] = "~/custom/overlay.settings"
        self.launcher.save_config(cfg)
        again = self.launcher.load_config()
        self.assertEqual(again["overlay_settings"], "~/custom/overlay.settings")
        self.assertEqual(self.launcher.overlay_settings_path(again),
                         str(self.root / "home" / "custom" / "overlay.settings"))

    def script_for(self, cfg) -> str:
        cfg = dict(cfg)
        cfg.setdefault("vfs_root", str(self.root / "USRDIR"))
        cfg.setdefault("elf", str(self.root / "EBOOT.ELF"))
        cfg["mods_dir"] = str(self.root / "mods")
        return self.launcher.build_launch_script(cfg, binary=self.root / "boot_gow2")

    def test_play_script_exports_resolved_path(self):
        cfg = self.launcher.load_config()
        cfg["overlay_settings"] = str(self.root / "with space" / "o.settings")
        script = self.script_for(cfg)
        expected = "export PS3_OVERLAY_SETTINGS=" + shlex.quote(cfg["overlay_settings"])
        self.assertIn(expected, script)
        default_script = self.script_for(self.launcher.default_config())
        self.assertIn("export PS3_OVERLAY_SETTINGS=" + shlex.quote(self.default_path()),
                      default_script)

    def test_caller_environment_wins(self):
        os.environ["PS3_OVERLAY_SETTINGS"] = "/explicit/from/env.settings"
        script = self.script_for(self.launcher.default_config())
        self.assertIn("export PS3_OVERLAY_SETTINGS=/explicit/from/env.settings", script)

    def test_persisted_graphics_are_not_masked_by_launcher_defaults(self):
        """Fullscreen/VSync env is exported only when the caller set it."""
        script = self.script_for(self.launcher.default_config())
        exec_at = script.index("\nexec ")
        probe = script[:exec_at] + "\nprintf '%s|%s|%s\\n' " \
            "\"${PS3_FULLSCREEN-unset}\" \"${PS3_METAL_VSYNC-unset}\" " \
            "\"${PS3_OVERLAY_SETTINGS-unset}\"\n"
        # Keep the real env_gow2.sh out of this: stub it.
        env_sh = str(self.launcher.HERE / "env_gow2.sh")
        probe = probe.replace(repr(env_sh), shlex.quote(str(self.root / "env_stub.sh")))
        (self.root / "env_stub.sh").write_text("PS3_METAL_VSYNC=1\n")
        probe = probe.replace(repr(self.launcher.HERE.as_posix()), shlex.quote(str(self.root)))
        base_env = {k: v for k, v in os.environ.items()
                    if k not in ("PS3_FULLSCREEN", "PS3_METAL_VSYNC")}
        out = subprocess.run(["/bin/bash", "-c", probe], env=base_env,
                             capture_output=True, text=True, check=True).stdout.strip()
        self.assertEqual(out, "unset|unset|" + self.default_path())
        out = subprocess.run(["/bin/bash", "-c", probe],
                             env=dict(base_env, PS3_FULLSCREEN="1", PS3_METAL_VSYNC="0"),
                             capture_output=True, text=True, check=True).stdout.strip()
        self.assertEqual(out, "1|0|" + self.default_path())


if __name__ == "__main__":
    unittest.main(verbosity=2)
