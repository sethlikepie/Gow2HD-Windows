#!/usr/bin/env python3
"""Offline: same env parse helper used by patch_type15_cc9d0_disc.py.

Contract (Python + injected C must match):
  ON  iff raw is non-None, non-empty, and first character is '1'.
  OFF for unset / empty / '0' / 'false' / 'off' / 'no' / anything else.
"""
from typing import Optional


def env_on(name: str, raw: Optional[str]) -> bool:
    """ON iff first char is '1' (unset/empty/0/false all OFF)."""
    del name  # name is for call-site clarity only
    if raw is None or raw == "":
        return False
    return raw[0] == "1"


assert env_on("PS3_TRACE_CC9D0", None) is False
assert env_on("PS3_TRACE_CC9D0", "0") is False
assert env_on("PS3_TRACE_CC9D0", "") is False
assert env_on("PS3_TRACE_CC9D0", "false") is False
assert env_on("PS3_TRACE_CC9D0", "off") is False
assert env_on("PS3_TRACE_CC9D0", "no") is False
assert env_on("PS3_TRACE_CC9D0", "true") is False
assert env_on("PS3_TRACE_CC9D0", "1") is True
assert env_on("PS3_TRACE_CC9D0", "10") is True  # first char '1'
assert env_on("PS3_TYPE15_DISC", None) is False
assert env_on("PS3_TYPE15_DISC", "0") is False
assert env_on("PS3_TYPE15_DISC", "1") is True

print("test_trace_cc9d0_env: PASS")
