"""E316 (fix, gated diagnostico PS3_F2B_REARM, OFF por defeito): re-arma o F2B stream fill
quando a MESMA FO e' reusada para um WAD maior (R_PermA 20MB) depois de um pequeno (R_LglScA 3072B).

Medido (E316): o F2B host-fill (g_f2b_fill_mfd/sz/file_pos) ficava preso no tamanho do R_LglScA
(3072B) -- file_pos=3072/3072 (EOF) -- e nunca streamava o R_PermA. Este patch: quando o site E275
WAD-HOST-READ regista um mfd novo p/ a FO ja' armada (sz maior), re-arma (mfd/sz, file_pos=0) e marca
g_f2b_fill_rearm; em f2b_stream_fill, ao ver rearm, limpa o ring (avail/cursor/write_pos=0) e restreama.
Medido: file_pos passa de 3072/3072 p/ 262144/20169344. Gated PS3_F2B_REARM, OFF.
"""
import sys, pathlib
ROOT = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parent.parent / "recomp_macos_e162"
F = ROOT / "ppu_recomp_001.cpp"
s = F.read_text()

a1 = "static uint32_t g_f2b_fill_stream = 0;"
add1 = "\nstatic int g_f2b_fill_rearm = 0; /* E316: FO reused for a bigger WAD (R_PermA); wipe ring + restream */"
if "g_f2b_fill_rearm" not in s:
    assert s.count(a1) == 1, a1
    s = s.replace(a1, a1 + add1)

a2 = "    g_f2b_fill_stream = stream;\n    int rounds = 0;"
rep2 = ("    g_f2b_fill_stream = stream;\n"
        "    if (g_f2b_fill_rearm) {\n"
        "        g_f2b_fill_rearm = 0;\n"
        "        vm_write32(stream + 0x10u, 0u); /* avail=0 */\n"
        "        vm_write32(stream + 0x8u,  0u); /* cursor=0 */\n"
        "        vm_write32(stream + 0x4u,  0u); /* write_pos=0 */\n"
        "    }\n"
        "    int rounds = 0;")
if "g_f2b_fill_rearm = 0;\n        vm_write32(stream + 0x10u, 0u)" not in s:
    assert s.count(a2) == 1, a2
    s = s.replace(a2, rep2)

a3 = "if(_mfd){ f2b_fo_mfd_del(_fo); f2b_fo_mfd_put(_fo,_mfd,_sz); vm_write32(_fo+0x48u,0u); vm_write32(_fo+0x4Cu,_sz); vm_write32(_fo+0x50u,1u);"
rep3 = (a3 +
        "\n                { static int _rr=-1; if(_rr<0){const char* _e=getenv(\"PS3_F2B_REARM\"); _rr=(_e&&*_e&&*_e!='0')?1:0;}"
        "\n                  if(_rr && _fo==g_f2b_fill_fo && _sz>g_f2b_fill_sz){"
        "\n                    g_f2b_fill_mfd=_mfd; g_f2b_fill_sz=_sz; g_f2b_fill_file_pos=0; g_f2b_fill_rearm=1;"
        "\n                    static int _rn=0; if(_rn++<8){ fprintf(stderr,\"[E316-REARM] fo=0x%08X mfd->%u sz->%u\\n\",_fo,_mfd,_sz); fflush(stderr);} } }")
if "E316-REARM" not in s:
    assert s.count(a3) == 1, a3
    s = s.replace(a3, rep3)

F.write_text(s)
print("patch_e316_f2b_rearm: applied")
