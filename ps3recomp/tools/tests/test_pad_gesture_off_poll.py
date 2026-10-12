#!/usr/bin/env python3
"""Source guard: the pad poll path never touches the Home gesture state.

cpgc_poll runs on the guest thread (cellPadGetData). Setting
GCControllerButtonInput.preferredSystemGestureState is a call into the game
controller daemon that can block, so it must happen once per controller on
connect (and for controllers already connected at init), off the poll path.
GameController objects cannot be built headless, so this checks the source.
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parents[2] / "libs" / "input" / "cellpad_gamecontroller.m"


def body(text, signature_re):
    m = re.search(signature_re, text)
    if not m:
        return None
    i = text.index("{", m.end() - 1)
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "{":
            depth += 1
        elif text[j] == "}":
            depth -= 1
            if depth == 0:
                return text[i:j + 1]
    return None


def main():
    text = SRC.read_text(encoding="utf-8")
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)   # comments may name the helper
    text = re.sub(r"//[^\n]*", "", text)
    fails = []

    def check(ok, msg):
        if not ok:
            fails.append(msg)

    poll_ctrl = body(text, r"static int poll_controller\([^)]*\)\s*\{")
    poll = body(text, r"void cpgc_poll\([^)]*\)\s*\{")
    init = body(text, r"void cpgc_init\(void\)\s*\{")
    helper = body(text, r"static void disable_home_gesture\([^)]*\)\s*\{")
    check(poll_ctrl is not None and poll is not None and init is not None, "poll/init functions found")
    for name, b in (("poll_controller", poll_ctrl), ("cpgc_poll", poll)):
        check(b is not None and "preferredSystemGestureState" not in b, f"{name} does no gesture-state work")
        check(b is not None and "disable_home_gesture" not in b, f"{name} does not call the gesture helper")
    check(helper is not None, "disable_home_gesture helper exists")
    check(helper is not None and "preferredSystemGestureState" in helper, "helper sets the gesture state")
    check(helper is not None and "dispatch_async" in helper, "helper runs asynchronously (never on the caller)")
    check(text.count("preferredSystemGestureState") == (helper or "").count("preferredSystemGestureState"),
          "the gesture state is only touched inside the helper")
    check(init is not None and "GCControllerDidConnectNotification" in init and "disable_home_gesture" in init,
          "init arms the connect observer and covers controllers already connected")
    check(init is not None and "GCController.controllers" in init, "init walks the already-connected controllers")

    for f in fails:
        print("FAIL:", f)
    print("PASS" if not fails else f"FAIL {len(fails)}")
    return 0 if not fails else 1


if __name__ == "__main__":
    sys.exit(main())
