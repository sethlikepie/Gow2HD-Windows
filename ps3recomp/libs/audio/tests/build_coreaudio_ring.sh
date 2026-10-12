#!/bin/sh
# Builda e roda o teste TDD do ring SPSC do backend CoreAudio.
set -e
cd "$(dirname "$0")"
cc -std=c11 -O2 -Wall -Wextra -I../.. test_coreaudio_ring.c -o /tmp/test_coreaudio_ring
/tmp/test_coreaudio_ring
