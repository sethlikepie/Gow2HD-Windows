#!/usr/bin/env python3
"""Gerador de vetores de PRECISAO para o gate PS3_SPU_FMA_ISA
(runtime/spu/spu_helpers.h), consumidos por test_spu_fma_precision_main.c.

Ao contrario dos gen_test_*.py normais deste diretorio, este NAO produz um ELF
de SPU nem passa pelo spu_lifter.py: exercita os helpers de C diretamente. O
seu papel e' derivar, com aritmetica EXATA (fractions.Fraction), os vetores de
fronteira -- em especial os casos ADVERSARIAIS de double-rounding em que a
abordagem ingenua `(double)a*b + (double)c` (arredonda para o double mais
proximo e so' depois trunca para float) diverge do resultado round-toward-zero
correto -- e emitir um header C com {a,b,c,esperado} para regressao permanente.

O "esperado" vem da referencia EXATA (exact_rtz_ftz sobre Fraction) para todo
resultado nao-nulo; para resultado zero, o sinal do zero segue as regras IEEE e
e' tomado do modelo validado (two_sum + dd_rtz_ftz), que reproduz o algoritmo do
header. O gerador cruza modelo vs. referencia exata em cada vetor nao-nulo como
auto-verificacao.

Uso: python gen_test_spu_fma_precision.py   (escreve spu_fma_precision_vectors.h)
run_tests.sh chama isto antes de compilar o teste.
"""
import os, struct, random
from fractions import Fraction

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "spu_fma_precision_vectors.h")

FLT_MIN = 2.0 ** -126


def bits_f32(b):
    return struct.unpack('<f', struct.pack('<I', b & 0xFFFFFFFF))[0]


def f32(x):
    return struct.unpack('f', struct.pack('f', float(x)))[0]


def f32_bits(x):
    return struct.unpack('<I', struct.pack('<f', float(x)))[0]


def flush_in(x):
    """FTZ de operando subnormal (SPU SP nao tem denormais na entrada)."""
    return (-0.0 if struct.pack('<f', x)[3] & 0x80 else 0.0) if abs(x) < FLT_MIN else x


# ---- referencia EXATA: round-toward-zero + flush-to-zero, sobre Fraction ----
def exact_rtz_ftz(fr):
    if fr == 0:
        return 0
    sign = 0 if fr > 0 else 1
    m = abs(fr)
    e = 0
    if m >= 1:
        while Fraction(2) ** (e + 1) <= m:
            e += 1
    else:
        while Fraction(2) ** e > m:
            e -= 1
    if e < -126:
        return sign << 31            # subnormal -> FTZ
    if e > 127:
        return (sign << 31) | 0x7F7FFFFF   # overflow -> satura FLT_MAX
    frac = m / (Fraction(2) ** e) - 1
    return (sign << 31) | ((e + 127) << 23) | (int(frac * (2 ** 23)) & 0x7FFFFF)


# ---- modelo do header (two_sum + dd_rtz_ftz) -- so' para o SINAL do zero ----
def two_sum(a, b):
    s = a + b
    bb = s - a
    return s, (a - (s - bb)) + (b - bb)


def dd_rtz_ftz(hi, lo):
    if hi == 0.0:
        return (1 << 31) if struct.pack('<d', hi)[7] & 0x80 else 0
    bits = struct.unpack('<Q', struct.pack('<d', hi))[0]
    sign = (bits >> 63) & 1
    rawexp = (bits >> 52) & 0x7FF
    mant52 = bits & ((1 << 52) - 1)
    if rawexp == 0x7FF:
        return (sign << 31) | 0x7F800000
    dexp = rawexp - 1023
    if dexp < -126:
        return sign << 31
    if dexp > 127:
        return (sign << 31) | 0x7F7FFFFF
    f0_bits = (sign << 31) | ((dexp + 127) << 23) | (mant52 >> 29)
    f0 = bits_f32(f0_bits)
    rem = abs(hi) - abs(float(f0))
    lo_aligned = -lo if sign else lo
    if lo_aligned < 0.0 and (-lo_aligned) > rem:
        prev = (f0_bits - 1) & 0xFFFFFFFF
        if ((prev >> 23) & 0xFF) == 0:
            return sign << 31
        return prev
    return f0_bits


def model(op, a, b, c):
    a, b, c = flush_in(a), flush_in(b), flush_in(c)
    if op == 'fma':
        hi, lo = two_sum(float(a) * float(b), float(c))
    elif op == 'fms':
        hi, lo = two_sum(float(a) * float(b), -float(c))
    elif op == 'fnms':
        hi, lo = two_sum(-(float(a) * float(b)), float(c))
    elif op == 'fa':
        hi, lo = two_sum(float(a), float(b))
    elif op == 'fs':
        hi, lo = two_sum(float(a), -float(b))
    elif op == 'fm':
        hi, lo = float(a) * float(b), 0.0
    else:
        raise ValueError(op)
    return dd_rtz_ftz(hi, lo)


def exact(op, a, b, c):
    a, b, c = flush_in(a), flush_in(b), flush_in(c)
    fa, fb, fc = Fraction(a), Fraction(b), Fraction(c)
    if op == 'fma':
        fr = fa * fb + fc
    elif op == 'fms':
        fr = fa * fb - fc
    elif op == 'fnms':
        fr = fc - fa * fb
    elif op == 'fa':
        fr = fa + fb
    elif op == 'fs':
        fr = fa - fb
    elif op == 'fm':
        fr = fa * fb
    else:
        raise ValueError(op)
    return exact_rtz_ftz(fr)


def expected(op, a, b, c):
    """Referencia exata para nao-zero; sinal do zero via modelo IEEE. Cruza os
    dois em cada caso nao-nulo como auto-verificacao do gerador."""
    e = exact(op, a, b, c)
    if (e & 0x7FFFFFFF) == 0:
        return model(op, a, b, c)          # zero: sinal IEEE do modelo
    m = model(op, a, b, c)
    assert m == e, (op, hex(f32_bits(a)), hex(f32_bits(b)), hex(f32_bits(c)), hex(m), hex(e))
    return e


# ---------------------------------------------------------------------------
vectors = []   # (op, name, a, b, c)


def add(op, name, a, b, c=0.0):
    vectors.append((op, name, f32(a), f32(b), f32(c)))


# ---- (1) curados: fronteiras documentadas ---------------------------------
P24_15 = float.fromhex('0x1.8p-24')   # 1.5 * 2^-24
P13 = float.fromhex('0x1p-13')
P70 = float.fromhex('0x1p-70')
add('fma', 'additive boundary no-carry (RN would round up)', 1.0, 1.0, P24_15)
add('fms', 'additive boundary no-carry', 1.0, 1.0, -P24_15)
add('fma', 'multiplicative boundary exponent-carry avoided',
    1.0 + P13, 1.0 - P13, 0.0)
add('fnms', 'multiplicative boundary sign flip', 1.0 + P13, 1.0 - P13, 0.0)
add('fma', 'subnormal RESULT flush to +0', P70, P70, 0.0)
add('fnms', 'subnormal RESULT flush to -0', P70, P70, 0.0)
add('fma', 'overflow saturates +FLT_MAX (not +inf)', bits_f32(0x7F7FFFFF), 2.0, 0.0)
add('fnms', 'overflow saturates -FLT_MAX (not -inf)', bits_f32(0x7F7FFFFF), 2.0, 0.0)
# denormal INPUT flush (finding #2): subnormal operand treated as signed zero IN
add('fm', 'denormal INPUT a flushed -> 0*b', bits_f32(0x00000001), 3.0, 0.0)
add('fma', 'denormal INPUT b flushed -> a*0+c', 7.0, bits_f32(0x00400000), 5.0)
add('fs', 'denormal INPUT a flushed -> 0 - b', bits_f32(0x007FFFFF), 2.5, 0.0)
add('fa', 'both operands normal (baseline)', 3.5, -1.25, 0.0)
add('fm', 'plain product baseline', 6.0, 0.5, 0.0)
add('fs', 'plain difference baseline', 10.0, 3.0, 0.0)

# ---- (2) adversariais FMA de double-rounding (construcao por reciproco) -----
# c = maior float abaixo de 1.0; p = a*b em (2^-24 - 2^-54, 2^-24): a soma exata
# fica logo abaixo de 1.0 mas RN-para-double sobe para 1.0 -> a abordagem ingenua
# truncaria 1.0; a correta trunca para c (0x3F7FFFFF).
_c = f32(1.0 - 2.0 ** -24)
_scale = 2.0 ** -24
_target = 2.0 ** -24
_lo = _target - 2.0 ** -54
_n = 0
for _ma in range(1 << 23, 1 << 24):
    _a = _ma * _scale
    _b = f32(_target / _a)
    _p = _a * _b
    if _lo < _p < _target:
        add('fma', 'double-rounding adversarial (naive would round up)', _a, _b, _c)
        # o simetrico negativo exercita o mesmo ramo com sinal invertido:
        add('fnms', 'double-rounding adversarial (sign-flipped)', _a, _b, _c)
        _n += 1
        if _n >= 12:
            break

# ---- (3) adversariais fa/fs (busca aleatoria limitada, deterministica) ------
def _rand_f32(rng):
    while True:
        b = rng.getrandbits(32)
        if (b >> 23) & 0xFF == 0xFF:
            continue
        return bits_f32(b)


def _naive_sum_bits(x, y):
    return dd_rtz_ftz(x + y, 0.0)   # ingenuo: RN-double e depois trunca


_rng = random.Random(20240723)
_fa_found = _fs_found = 0
for _ in range(60000):
    x = flush_in(_rand_f32(_rng))
    y = flush_in(_rand_f32(_rng))
    if _fa_found < 4 and model('fa', x, y, 0.0) != _naive_sum_bits(x, y):
        # so' guarda se a referencia exata bate com o modelo (nao-nulo)
        if (exact('fa', x, y, 0.0) & 0x7FFFFFFF) != 0:
            add('fa', 'double-rounding adversarial (add)', x, y)
            _fa_found += 1
    if _fs_found < 4 and model('fs', x, y, 0.0) != dd_rtz_ftz(x - y, 0.0):
        if (exact('fs', x, y, 0.0) & 0x7FFFFFFF) != 0:
            add('fs', 'double-rounding adversarial (sub)', x, y)
            _fs_found += 1
    if _fa_found >= 4 and _fs_found >= 4:
        break

# ---- (4) lote aleatorio amplo por operacao (regressao) ---------------------
_rng2 = random.Random(0xC0FFEE)
for _op in ('fma', 'fms', 'fnms', 'fa', 'fs', 'fm'):
    _got = 0
    while _got < 120:
        a = _rand_f32(_rng2)
        b = _rand_f32(_rng2)
        c = _rand_f32(_rng2) if _op in ('fma', 'fms', 'fnms') else 0.0
        add(_op, 'random regression', a, b, c)
        _got += 1

# ---------------------------------------------------------------------------
lines = []
lines.append("/* AUTO-GENERATED by gen_test_spu_fma_precision.py -- DO NOT EDIT.")
lines.append(" * Vetores de precisao do gate PS3_SPU_FMA_ISA (round-toward-zero +")
lines.append(" * flush-to-zero, com flush de operando subnormal). 'expected' e' o")
lines.append(" * resultado EXATO (fractions.Fraction) exceto o sinal do zero, que segue")
lines.append(" * IEEE. Regenerar: python runtime/spu/tests/gen_test_spu_fma_precision.py */")
lines.append("#ifndef SPU_FMA_PRECISION_VECTORS_H")
lines.append("#define SPU_FMA_PRECISION_VECTORS_H")
lines.append("typedef struct { const char* op; const char* name;")
lines.append("                 uint32_t a, b, c, expected; } spu_fma_vec_t;")
lines.append("static const spu_fma_vec_t SPU_FMA_VECTORS[] = {")
for op, name, a, b, c in vectors:
    exp = expected(op, a, b, c)
    lines.append('  {"%s", "%s", 0x%08X, 0x%08X, 0x%08X, 0x%08X},' %
                 (op, name.replace('"', "'"), f32_bits(a), f32_bits(b), f32_bits(c), exp))
lines.append("};")
lines.append("enum { SPU_FMA_VECTORS_N = %d };" % len(vectors))
lines.append("#endif /* SPU_FMA_PRECISION_VECTORS_H */")

with open(OUT, "w") as f:
    f.write("\n".join(lines) + "\n")

print("wrote %s (%d vectors)" % (OUT, len(vectors)))
