#!/usr/bin/env python3
"""apply_lift_patches.py -- the Windows port's changes to the LIFTED PPU code.

The lifted sources (gow2-recomp/recomp_macos_e435/ppu_recomp*.{h,cpp}) are generated from the player's
EBOOT.ELF at build time and are never part of this repository, so the port's few changes to them live
here as idempotent source transforms, applied by build.py right after the kit's lift has been verified
against gow2-recomp/kit/ppu_lift.sha256 (same model as upstream's gow2-recomp/recomp_mid_v2/patch_*.py).

  lift_v61_vm_inline.py  inline guest-memory reads in lifted code      (WINPORT_VM_INLINE)
  lift_v63_wstore.py     inline guest-memory stores in lifted code     (WINPORT_VM_INLINE_W)
  lift_v65_frame_dt.py   game clock follows real time above 60 fps     (WINPORT_DT_MIN_US)
  lift_v66_ui_tick.py    UI tick counter in real time (off by default)  (WINPORT_UI_DT)
  lift_v67_ui_time.py    Flash UI timeline in real time                (WINPORT_UI_RT)
  lift_v84a_lift_vm.py   cheaper inline guest access: per-block base tables, TBAA, inline lbz
                         (compile time; the lift is built with -fno-auto-import, see cbuild.py)

Each script was written as an engine patch for the working tree (apply_all(<ps3recomp dir>)); its
engine-side half is already in this repository's sources, so on this tree it only reports "already
patched" for the engine files and patches the lift at <ps3recomp>/../gow2-recomp/recomp_macos_e435.

Usage: apply_lift_patches.py <ps3recomp dir of the build tree>
"""
import importlib, pathlib, sys

HERE = pathlib.Path(__file__).resolve().parent
ORDER = ["lift_v61_vm_inline", "lift_v63_wstore", "lift_v65_frame_dt", "lift_v66_ui_tick", "lift_v67_ui_time",
         "lift_v84a_lift_vm"]

if __name__ == "__main__":
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(HERE))
    engine = pathlib.Path(sys.argv[1])
    for name in ORDER:
        importlib.import_module(name).apply_all(engine)
