#!/usr/bin/env python3
"""Idempotent M3 diagnostic: yield (and optional skip) on LIVE TYPE15 product
ticks in func_000CC9D0.

Context (M0–M2):
  UNSTICK only skips pin-shell products (0x47D00800–0x47D00C00) and yields
  there. Live product 0x42F85AE4 is *not* skipped; main burns CPU in the
  f4=f54=0 path without releasing the giant lock. M1 killed pure yield as
  *sole* cause (H4 DEAD) when list was empty — re-test after M2 CLOSE-PRESERVE.

Env (default OFF):
  PS3_CC9D0_YIELD=1  — release giant lock + usleep every 1024 live-product ticks
  PS3_CC9D0_LIVE_SKIP=1 — also early-return the body (like pin-shell SKIP) so
                          the parent walk can finish cheaply. Diagnostic only;
                          permanent skip of live product is NOT a product fix.

Target: recomp_macos_v2/ppu_recomp_000.cpp func_000CC9D0 UNSTICK block.
"""
from __future__ import annotations

import pathlib
import re
import sys

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent
TARGET = ROOT / "recomp_macos_v2" / "ppu_recomp_000.cpp"

MARKER = "/* M3: PS3_CC9D0_YIELD / PS3_CC9D0_LIVE_SKIP (live product; default OFF) */"

INJECT = r'''
        /* M3: PS3_CC9D0_YIELD / PS3_CC9D0_LIVE_SKIP (live product; default OFF) */
        { static int s_m3y = -1, s_m3s = -1;
          if (s_m3y < 0) {
            const char* ey = getenv("PS3_CC9D0_YIELD");
            const char* es = getenv("PS3_CC9D0_LIVE_SKIP");
            s_m3y = (ey && *ey == '1') ? 1 : 0;
            s_m3s = (es && *es == '1') ? 1 : 0;
          }
          if (s_m3y || s_m3s) {
            uint32_t th = (uint32_t)ctx->gpr[31];
            uint32_t prod = (th >= 0x10000u && th < 0x4F000000u)
                ? vm_read32(th + 0x8u) : 0u;
            /* Live non-pin product only (pin-shell already handled above). */
            int live = prod != 0u
                && !(prod >= 0x47D00800u && prod < 0x47D00C00u);
            if (live) {
              { static unsigned y = 0;
                if (s_m3y && ((++y & 0x3FFu) == 0u)) {
                  { static int n = 0;
                    if (n++ < 8)
                      fprintf(stderr,
                              "[TYPE15] CC9D0-YIELD this=0x%08X prod=0x%08X\n",
                              th, prod);
                  }
                  ppu_giant_lock_release();
#ifndef _WIN32
                  usleep(500);
#endif
                  ppu_giant_lock_acquire();
                }
              }
              if (s_m3s) {
                { static int n = 0;
                  if (n++ < 8)
                    fprintf(stderr,
                            "[TYPE15] CC9D0-LIVE-SKIP this=0x%08X prod=0x%08X\n",
                            th, prod);
                }
                ctx->gpr[0] = vm_read64(ctx->gpr[1] + 0xF0);
                ctx->gpr[28] = vm_read64(ctx->gpr[1] + 0xB8);
                ctx->gpr[29] = vm_read64(ctx->gpr[1] + 0xC0);
                ctx->lr = ctx->gpr[0];
                ctx->gpr[30] = vm_read64(ctx->gpr[1] + 0xC8);
                ctx->gpr[31] = vm_read64(ctx->gpr[1] + 0xD0);
                { uint64_t tmp = vm_read64(ctx->gpr[1] + 0xD8);
                  memcpy(&ctx->fpr[31], &tmp, 8); }
                ctx->gpr[1] = (int64_t)(int32_t)(ctx->gpr[1] + 0xE0);
                return;
              }
            }
          }
        }
'''


def main() -> int:
    if not TARGET.is_file():
        print(f"missing {TARGET}", file=sys.stderr)
        return 2
    src = TARGET.read_text(errors="replace")
    if MARKER in src:
        print("already applied:", TARGET)
        return 0

    # Insert immediately after the pin-shell UNSTICK block's closing braces,
    # before the TRACE_CC9D0 block.
    needle = (
        "              ctx->gpr[1] = (int64_t)(int32_t)(ctx->gpr[1] + 0xE0);\n"
        "              return;\n"
        "            }\n"
        "          }\n"
        "        }\n"
        "        { static int on=-1; if(on<0){extern char* getenv(const char*);\n"
        '            const char* e=getenv("PS3_TRACE_CC9D0");'
    )
    if needle not in src:
        # try alternate: after UNSTICK, before TRACE (more flexible)
        m = re.search(
            r'(                          "\(pin-shell only\)\\n",\n'
            r"                          th, prod\);\n"
            r"              \}\n"
            r"              /\* Cooperative yield:.*?\n"
            r"              return;\n"
            r"            \}\n"
            r"          \}\n"
            r"        \}\n)"
            r"(\s*\{ static int on=-1;)",
            src,
            re.S,
        )
        if not m:
            print("needle missing (lift shape changed?)", file=sys.stderr)
            return 1
        # Use simple string after pin-shell block
        # Fall through to find unique anchor
        anchor = (
            '                          "[TYPE15] CC9D0-SKIP this=0x%08X prod=0x%08X "\n'
            '                          "(pin-shell only)\\n",\n'
        )
        if anchor not in src:
            print("anchor missing", file=sys.stderr)
            return 1
        # Find the closing of UNSTICK after the anchor, then TRACE block
        idx = src.find(anchor)
        rest = src[idx:]
        # Find first occurrence of TRACE after anchor
        tmark = '{ static int on=-1; if(on<0){extern char* getenv(const char*);\n            const char* e=getenv("PS3_TRACE_CC9D0");'
        tidx = rest.find(tmark)
        if tidx < 0:
            print("TRACE marker missing after UNSTICK", file=sys.stderr)
            return 1
        # Walk back to start of TRACE's preceding whitespace line
        abs_t = idx + tidx
        # insert before TRACE block
        new_src = src[:abs_t] + INJECT + src[abs_t:]
        TARGET.write_text(new_src)
        print("applied (flex):", TARGET)
        return 0

    new_src = src.replace(
        needle,
        (
            "              ctx->gpr[1] = (int64_t)(int32_t)(ctx->gpr[1] + 0xE0);\n"
            "              return;\n"
            "            }\n"
            "          }\n"
            "        }\n"
            + INJECT
            + "        { static int on=-1; if(on<0){extern char* getenv(const char*);\n"
            '            const char* e=getenv("PS3_TRACE_CC9D0");'
        ),
        1,
    )
    if new_src == src:
        print("replace failed", file=sys.stderr)
        return 1
    TARGET.write_text(new_src)
    print("applied:", TARGET)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
