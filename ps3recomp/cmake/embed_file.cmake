# Usage: cmake -DINPUT=<file> -DOUTPUT=<file.c> -DSYMBOL=<name> -P embed_file.cmake
# Emits `const unsigned char SYMBOL[]` and `const size_t SYMBOL_size`.
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR size "${hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUTPUT}"
    "/* Generated from ${INPUT} by cmake/embed_file.cmake. Do not edit. */\n"
    "#include <stddef.h>\n"
    "const unsigned char ${SYMBOL}[] = {${bytes}};\n"
    "const size_t ${SYMBOL}_size = ${size};\n")
