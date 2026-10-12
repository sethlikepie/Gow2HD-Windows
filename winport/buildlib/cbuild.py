"""cbuild.py -- compile the engine runtime and link the game exe with llvm-mingw, without cmake/ninja/bash.

runtime(): the ps3recomp_runtime static library exactly as ps3recomp/CMakeLists.txt builds it for Windows
           (same source globs and exclusions, same flags as the generated build.ninja: Release, -O3,
           -march=x86-64-v2), minus the fork's own D3D12 backend (the port links winport/d3d12 instead).
exe():     winport's former build_windows.sh: lifted PPU chunks, runtime PPU + GoW2 host sources, HLE NID
           table, SPU programs, SCREAM native jobs, Windows boot host, the D3D12 renderer, link.
"""
import concurrent.futures
import glob
import os
import re
import subprocess

TOOL = "x86_64-w64-mingw32-"


class BuildError(Exception):
    pass


class Toolchain:
    def __init__(self, root):
        self.bin = os.path.join(root, "bin")
        self.cc = os.path.join(self.bin, TOOL + "clang.exe")
        self.cxx = os.path.join(self.bin, TOOL + "clang++.exe")
        self.ar = os.path.join(self.bin, "llvm-ar.exe")
        for t in (self.cc, self.cxx, self.ar):
            if not os.path.isfile(t):
                raise BuildError("toolchain file missing: " + t)


def _needs(obj, srcs, force):
    if force or not os.path.isfile(obj):
        return True
    t = os.path.getmtime(obj)
    return any(os.path.getmtime(s) > t for s in srcs)


def run_jobs(jobs, n, log, progress=None, label=""):
    """jobs: list of (argv, obj, [srcs], force). Runs the ones that are out of date, n at a time."""
    todo = [j for j in jobs if _needs(j[1], j[2], j[3])]
    if not todo:
        return 0
    done = [0]
    errors = []

    def one(j):
        argv, obj = j[0], j[1]
        os.makedirs(os.path.dirname(obj), exist_ok=True)
        r = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return j, r.returncode, r.stdout.decode("utf-8", "replace")

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, n)) as ex:
        for j, rc, out in ex.map(one, todo):
            done[0] += 1
            if out.strip() or rc:
                with open(log, "a", encoding="utf-8") as f:
                    f.write("$ %s\n%s\n" % (subprocess.list2cmdline(j[0]), out))
            if rc:
                errors.append((j[1], out))
                try:
                    os.remove(j[1])
                except OSError:
                    pass
            if progress:
                progress(done[0], len(todo))
    if errors:
        o, out = errors[0]
        raise BuildError("%s: %d compile error(s); first: %s\n%s" % (label, len(errors), os.path.basename(o),
                                                                    "\n".join(out.splitlines()[-15:])))
    return len(todo)


# ------------------------------------------------------------------------------------------- runtime
def runtime_sources(ps3):
    def rglob(sub, ext):
        return sorted(glob.glob(os.path.join(ps3, sub, "**", "*" + ext), recursive=True))
    hle = []
    for d in ("system", "filesystem", "input", "audio", "video", "network", "spurs", "sync", "codec", "font",
              "misc", "hardware"):
        hle += rglob(os.path.join("libs", d), ".c")
    norm = lambda p: p.replace("\\", "/")
    hle = [p for p in hle if not re.search(r"/sceNpCommerce\.c$", norm(p))]
    hle = [p for p in hle if "/tests/" not in norm(p)]
    hle = [p for p in hle if not re.search(r"/rsx_overlay_(d3d12|metal)\.(c|m)$", norm(p))]
    for extra in ("libs/filesystem/ps3_mods.c", "libs/video/rsx_overlay.cpp", "libs/video/rsx_overlay_shell.cpp",
                  "libs/video/rsx_overlay_shell_draw.cpp", "libs/video/rsx_overlay_assets.c",
                  "libs/video/rsx_overlay_ui_sound.c"):
        hle.append(os.path.join(ps3, *extra.split("/")))
    seen, uniq = set(), []
    for p in hle:
        k = os.path.normcase(os.path.abspath(p))
        if k not in seen:
            seen.add(k)
            uniq.append(p)
    hle = uniq
    d3d_overlay = os.path.join(ps3, "libs", "video", "rsx_overlay_d3d12.c")
    if os.path.isfile(d3d_overlay):
        hle.append(d3d_overlay)
    # no Vulkan SDK: the Vulkan backend is left out (as CMake does when Vulkan is not found)
    hle = [p for p in hle if os.path.basename(p) != "rsx_vulkan_backend.c"]
    rt = rglob("runtime", ".c") + rglob("runtime", ".cpp")
    rt = [p for p in rt if not re.search(r"/tests/|/runtime/ppu/|/runtime/host/", norm(p))]
    return hle + rt


def embed_file(src, out, symbol):
    """cmake/embed_file.cmake: `const unsigned char SYMBOL[] = {0x..,...};` + SYMBOL_size."""
    data = open(src, "rb").read()
    body = "".join("0x%02x," % b for b in data)
    txt = ("/* Generated from %s by cmake/embed_file.cmake. Do not edit. */\n#include <stddef.h>\n"
           "const unsigned char %s[] = {%s};\nconst size_t %s_size = %d;\n"
           % (src.replace("\\", "/"), symbol, body, symbol, len(data)))
    if not os.path.isfile(out) or open(out, encoding="utf-8").read() != txt:
        with open(out, "w", encoding="utf-8", newline="\n") as f:
            f.write(txt)


def runtime(tc, ps3, obj_dir, out_lib, jobs, log, force, progress=None):
    inc = ["-I" + os.path.join(ps3, d) for d in ("include", "libs/system", "libs/filesystem", "libs/input",
                                                 "libs/audio", "libs/video", "libs/network", "libs/spurs",
                                                 "libs/sync", "libs/codec", "libs/font", "libs/misc",
                                                 "runtime/spu", "third_party/imgui")]
    cflags = ["-march=x86-64-v2", "-O3", "-DNDEBUG", "-std=gnu17", "-Wall", "-Wextra", "-Wno-unused-parameter",
              "-fno-strict-aliasing"]
    cxxflags = ["-march=x86-64-v2", "-O3", "-DNDEBUG", "-std=gnu++20", "-Wall", "-Wextra",
                "-Wno-unused-parameter", "-fno-strict-aliasing"]
    work = []
    objs = []

    def objname(src, sub):
        rel = os.path.relpath(src, ps3).replace("\\", "/")
        return os.path.join(obj_dir, sub, rel + ".obj")

    for s in runtime_sources(ps3):
        if os.path.basename(s) == "rsx_d3d12_backend.c":
            continue   # the fork's own D3D12 backend: replaced by winport/d3d12 (build_windows.sh dropped it too)
        o = objname(s, "runtime")
        is_cxx = s.endswith(".cpp")
        argv = [tc.cxx if is_cxx else tc.cc] + inc + (cxxflags if is_cxx else cflags) + ["-o", o, "-c", s]
        work.append((argv, o, [s], force))
        objs.append(o)
    for n in ("imgui", "imgui_draw", "imgui_tables", "imgui_widgets"):
        s = os.path.join(ps3, "third_party", "imgui", n + ".cpp")
        o = objname(s, "imgui")
        argv = [tc.cxx, "-I" + os.path.join(ps3, "third_party", "imgui"), "-march=x86-64-v2", "-O3", "-DNDEBUG",
                "-std=gnu++20", "-o", o, "-c", s]
        work.append((argv, o, [s], force))
        objs.append(o)
    gen = os.path.join(obj_dir, "generated")
    os.makedirs(gen, exist_ok=True)
    for font, sym in (("Inter-Regular.ttf", "rsx_overlay_inter_regular"),
                      ("Inter-SemiBold.ttf", "rsx_overlay_inter_semibold")):
        c = os.path.join(gen, sym + ".c")
        embed_file(os.path.join(ps3, "third_party", "inter", font), c, sym)
        o = os.path.join(obj_dir, "fonts", sym + ".c.obj")
        work.append(([tc.cc, "-march=x86-64-v2", "-O3", "-DNDEBUG", "-std=gnu17", "-o", o, "-c", c], o, [c], force))
        objs.append(o)
    n = run_jobs(work, jobs, log, progress, "runtime")
    if n or not os.path.isfile(out_lib):
        if os.path.isfile(out_lib):
            os.remove(out_lib)
        rsp = out_lib + ".rsp"
        with open(rsp, "w", encoding="utf-8") as f:
            f.write("\n".join('"%s"' % o.replace("\\", "/") for o in objs) + "\n")
        r = subprocess.run([tc.ar, "qc", out_lib, "@" + rsp], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if r.returncode:
            raise BuildError("llvm-ar failed: " + r.stdout.decode("utf-8", "replace")[-800:])
        subprocess.run([os.path.join(tc.bin, "llvm-ranlib.exe"), out_lib], check=True)
    return len(objs)


# ------------------------------------------------------------------------------------------- exe
def _grep_files(files, pattern):
    rx = re.compile(pattern, re.M)
    for f in files:
        if rx.search(open(f, encoding="utf-8", errors="surrogateescape").read()):
            return True
    return False


def exe(tc, kit, winport, obj_dir, runtime_lib, out_exe, py, pyenv, jobs, log, force, march="-march=x86-64-v3",
        lift_opt="-O1", lift_flags=("-fno-auto-import",), lift_tag="1a", host_opt="-O2", spu_opt="-O3", scream_opt="-O2", progress=lambda m, *a: None):
    here = os.path.join(kit, "gow2-recomp")
    ps3 = os.path.join(kit, "ps3recomp")
    lift = os.path.join(here, "recomp_macos_e435")
    obj = obj_dir
    os.makedirs(obj, exist_ok=True)
    inc = ["-I", lift, "-I", os.path.join(ps3, "include")] + sum(
        (["-I", os.path.join(ps3, d)] for d in ("runtime/ppu", "runtime/syscalls", "runtime/spu", "runtime/prx",
                                                "runtime/memory", "libs/system", "libs/spurs", "libs/sync",
                                                "libs/video", "libs/audio", "libs/network", "libs/codec")), [])
    cxxf = ["-std=c++20", march, "-w"]
    cf = ["-std=c11", march, "-w"]

    def pyrun(*a):
        r = subprocess.run([py] + [str(x) for x in a], env=pyenv, cwd=here, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT)
        out = r.stdout.decode("utf-8", "replace")
        with open(log, "a", encoding="utf-8") as f:
            f.write("$ %s\n%s\n" % (" ".join(str(x) for x in a), out))
        if r.returncode:
            raise BuildError("%s failed:\n%s" % (os.path.basename(str(a[0])), out[-1500:]))

    # 1. lifted chunks. -fno-auto-import (v84): no mingw .refptr indirection for the runtime globals the inline
    #    guest-access code reads (everything is linked statically); objects are tagged "1a" so an older build's
    #    -O1 objects are never reused.
    progress("compiling the lifted PPU code (7 large files)")
    chunks = sorted(glob.glob(os.path.join(lift, "ppu_recomp_*.cpp")))
    if len(chunks) != 7:
        raise BuildError("expected 7 lifted PPU chunks, found %d" % len(chunks))
    hdr = os.path.join(lift, "ppu_recomp.h")
    lift_objs, work = [], []
    for c in chunks:
        o = os.path.join(obj, os.path.basename(c) + "." + lift_tag + ".o")
        work.append(([tc.cxx, "-std=c++20", march, lift_opt] + list(lift_flags) + ["-DWP_LIFT_TU", "-w", "-c", "-I", lift, "-I",
                      os.path.join(ps3, "include"), "-I", os.path.join(ps3, "runtime", "ppu"), c, "-o", o],
                     o, [c, hdr], force))
        lift_objs.append(o)
    run_jobs(work, jobs, log, lambda d, t: progress("lifted PPU code %d/%d" % (d, t)), "lifted PPU code")

    # 2. runtime PPU + GoW2 host sources
    progress("compiling the PPU runtime and host sources")
    work, ppu_objs, extra = [], [], []
    for src in ("ppu_loader", "ppu_imports", "ppu_hle", "ppu_sysprx", "ppu_fs"):
        s = os.path.join(ps3, "runtime", "ppu", src + ".cpp")
        o = os.path.join(obj, src + ".o")
        work.append(([tc.cxx] + cxxf + [host_opt, "-c"] + inc + [s, "-o", o], o, [s, hdr], force))
        ppu_objs.append(o)
    for src in ("ppu_icall_ascii", "ppu_vm_fast_policy", "ppu_p10_ctr"):
        s = os.path.join(ps3, "runtime", "ppu", src + ".c")
        o = os.path.join(obj, src + ".o")
        work.append(([tc.cc] + cf + [host_opt, "-c"] + inc + [s, "-o", o], o, [s], force))
        ppu_objs.append(o)
    # the same anti-duplicate-symbol guards as build_macos.sh / build_windows.sh
    if not _grep_files(chunks, r'extern "C" int ps3_factory_repair_vt\(uint32_t obj\) *\{'):
        s = os.path.join(here, "host_gow2_factory.cpp")
        o = os.path.join(obj, "host_gow2_factory.o")
        work.append(([tc.cxx] + cxxf + [host_opt, "-c"] + inc + [s, "-o", o], o, [s, hdr], force))
        extra.append(o)
    f2b = (_grep_files(chunks, r"f2b_stream_ensure\(uint32_t type_sys\) *\{")
           or _grep_files(chunks, r"^static .*\bf2b_(fo_mfd_put|fo_mfd_get|fo_sz_get|stream_fill|stream_pre_consume)\b")
           or _grep_files(chunks, r"^static .*\bg_f2b_(fo_mfd_fo|fo_mfd_fd|fo_mfd_sz|fo_mfd_n|natural_movie_fo|fill_fo|"
                                  r"fill_mfd|fill_sz|fill_file_pos|fill_stream)\b"))
    if not f2b:
        s = os.path.join(here, "host_gow2_f2b.c")
        o = os.path.join(obj, "host_gow2_f2b.o")
        work.append(([tc.cc] + cf + [host_opt, "-c"] + inc + [s, "-o", o], o, [s], force))
        extra.append(o)
    if not _grep_files(chunks, r"(^|[^A-Za-z0-9_])gow2_midasm_[A-Za-z0-9_]+[^\S\n]*\([^;\n]*\)[^\S\n]*(\{|$)"):
        s = os.path.join(here, "hooks", "gow2_midasm_hooks.cpp")
        o = os.path.join(obj, "gow2_midasm_hooks.o")
        work.append(([tc.cxx] + cxxf + [host_opt, "-c"] + inc + ["-I", os.path.join(here, "hooks"), s, "-o", o],
                     o, [s, hdr], force))
        extra.append(o)
    ovr = os.path.join(here, "hooks", "gow2_func_overrides.cpp")
    otxt = open(ovr, encoding="utf-8", errors="surrogateescape").read()
    names = set(re.findall(r"GOW2_FUNC_OVERRIDE\s*\(\s*(func_[0-9A-Fa-f]{8})", otxt))
    names |= set(m.group(2) for m in re.finditer(
        r"(^|[^A-Za-z0-9_])(func_[0-9A-Fa-f]{8})[^\S\n]*\([^\S\n]*ppu_context[^\S\n]*\*[^\S\n]*ctx[^\S\n]*\)[^\S\n]*(\{|$)",
        otxt, re.M))
    strong = [n for n in sorted(names)
              if _grep_files(chunks, r"(^|[^A-Za-z0-9_])%s[^\S\n]*\([^;\n]*\)[^\S\n]*(\{|$)" % n)]
    if names and not strong:
        o = os.path.join(obj, "gow2_func_overrides.o")
        work.append(([tc.cxx] + cxxf + [host_opt, "-c"] + inc + ["-I", os.path.join(here, "hooks"), ovr, "-o", o],
                     o, [ovr, hdr], force))
        extra.append(o)
    with open(log, "a", encoding="utf-8") as f:
        f.write("host guards: extra=%s overrides strong=%s\n" % ([os.path.basename(x) for x in extra], strong))
    for src in ("host_res_inflate", "host_wad_tex"):
        s = os.path.join(ps3, "runtime", "ppu", src + ".c")
        o = os.path.join(obj, src + ".o")
        work.append(([tc.cc] + cf + [host_opt, "-c"] + inc + ["-I", os.path.join(ps3, "libs", "video"), s, "-o", o],
                     o, [s], force))
        extra.append(o)

    # 3. HLE NID table
    libs = sorted({os.path.splitext(os.path.basename(p))[0] for p in glob.glob(os.path.join(ps3, "libs", "*", "*.c"))}
                  - {"sceNpCommerce"})
    os.makedirs(os.path.join(obj, "gen"), exist_ok=True)
    nids = os.path.join(obj, "gen", "ppu_hle_nids.cpp")
    pyrun(os.path.join(ps3, "tools", "gen_hle_nids.py"), "--out", nids, *libs)
    o = os.path.join(obj, "ppu_hle_nids.o")
    work.append(([tc.cxx] + cxxf + [host_opt, "-c"] + inc + ["-I", os.path.join(ps3, "libs"), nids, "-o", o],
                 o, [nids], True))
    nid_obj = o

    # 3b. SPU programs (the kit's patch/verify scripts run on spu_lifted first, as in build_macos.sh)
    progress("SPU programs")
    spu_objs = []
    for d in sorted(glob.glob(os.path.join(here, "spu_lifted", "spu?_v2"))):
        if not os.path.isfile(os.path.join(d, "spu_recomp.c")):
            continue
        n = os.path.basename(d)
        pyrun(os.path.join(here, "recomp_mid_v2", "patch_spu_halfword_cond.py"), d)
        if n == "spu0_v2":
            pyrun(os.path.join(here, "recomp_mid_v2", "patch_spu0_observed_entries.py"), d)
            pyrun(os.path.join(ps3, "tools", "verify_spu_indirect_entries.py"), d, "spu0_", "--observed",
                  os.path.join(here, "recomp_mid_v2", "spu0_observed_indirect_targets.lst"))
        elif n == "spu1_v2":
            pyrun(os.path.join(here, "recomp_mid_v2", "patch_spu1_observed_entries.py"), d)
            pyrun(os.path.join(ps3, "tools", "verify_spu_indirect_entries.py"), d, "spu1_", "--observed",
                  os.path.join(here, "recomp_mid_v2", "spu1_observed_indirect_targets.lst"))
        else:
            pyrun(os.path.join(ps3, "tools", "verify_spu_indirect_entries.py"), d, n[:-3] + "_")
        o = os.path.join(obj, n + "_spu_recomp.o")
        s = os.path.join(d, "spu_recomp.c")
        work.append(([tc.cc] + cf + [spu_opt, "-c", "-I", d, "-I", os.path.join(ps3, "runtime", "spu"), "-I",
                      os.path.join(ps3, "include"), s, "-o", o], o, [s, os.path.join(d, "spu_recomp.h")], force))
        spu_objs.append(o)
    s = os.path.join(here, "recomp_mid_v2", "gow2_spu_register.c")
    o = os.path.join(obj, "gow2_spu_register.o")
    work.append(([tc.cc] + cf + [spu_opt, "-c", "-I", os.path.join(ps3, "runtime", "spu"), "-I",
                  os.path.join(ps3, "include"), s, "-o", o], o, [s], force))
    spu_objs.append(o)
    scr = os.path.join(kit, "scream_lift")
    if os.path.isfile(os.path.join(scr, "scr_jobs.c")):
        for s in sorted(glob.glob(os.path.join(scr, "scr*.c"))) + [os.path.join(winport, "scream", "scream_native_rt.c")]:
            o = os.path.join(obj, "wp29_" + os.path.splitext(os.path.basename(s))[0] + ".o")
            work.append(([tc.cc] + cf + [scream_opt, "-c", "-I", scr, "-I", os.path.join(ps3, "runtime", "spu"), "-I",
                          os.path.join(ps3, "include"), s, "-o", o], o, [s, os.path.join(scr, "scr_native.h")], force))
            spu_objs.append(o)

    # 4. Windows boot host + release start-up
    host_objs = []
    s = os.path.join(ps3, "runtime", "ppu", "tests", "boot_main.cpp")
    o = os.path.join(obj, "boot_main.o")
    work.append(([tc.cxx] + cxxf + [host_opt, "-c"] + inc + [s, "-o", o], o, [s, hdr], force))
    host_objs.append(o)
    for n in ("movie_eos_arm", "gow2_overlay_provider"):
        s = os.path.join(here, n + ".c")
        o = os.path.join(obj, n + ".o")
        work.append(([tc.cc] + cf + [host_opt, "-c", "-I", here, "-I", os.path.join(ps3, "libs", "video"), s, "-o", o],
                     o, [s], force))
        host_objs.append(o)
    for n in ("win_compat", "wp_release"):
        s = os.path.join(winport, "host", n + ".c")
        o = os.path.join(obj, n + ".o")
        work.append(([tc.cc] + cf + [host_opt, "-c", s, "-o", o], o, [s], force))
        host_objs.append(o)

    # 4b. the D3D12 renderer (winport/d3d12, upstream ps3recomp's renderer adapted to the fork)
    v = os.path.join(winport, "d3d12")
    render_objs = []
    vhdrs = sorted(glob.glob(os.path.join(v, "*.h")))
    for f in ("rsx_d3d12_backend", "rsx_fp_decompiler", "rsx_vp_decompiler", "rsx_texture_layout", "rsx_vertex_fetch"):
        s = os.path.join(v, f + ".c")
        o = os.path.join(obj, "up_" + f + ".o")
        work.append(([tc.cc, "-std=c11", march, host_opt, "-DNDEBUG", "-w", "-Dgetenv=wp_getenv_cached", "-include",
                      os.path.join(v, "wp_getenv.h"), "-include", os.path.join(v, "up_rename.h"), "-I", v, "-I",
                      os.path.join(ps3, "libs", "video"), "-I", os.path.join(ps3, "include"), "-I",
                      os.path.join(ps3, "runtime", "ppu"), "-I", os.path.join(ps3, "runtime", "spu"), "-I",
                      os.path.join(ps3, "libs", "input"), "-c", s, "-o", o], o, [s] + vhdrs, force))
        render_objs.append(o)
    s = os.path.join(v, "d3d12_fork_shim.c")
    o = os.path.join(obj, "d3d12_fork_shim.o")
    work.append(([tc.cc] + cf + [host_opt, "-c", s, "-o", o], o, [s], force))
    render_objs.append(o)

    progress("compiling host, SPU and renderer sources")
    run_jobs(work, jobs, log, lambda d, t: progress("host/SPU/renderer %d/%d" % (d, t)), "host sources")

    # 5. link
    progress("linking")
    rsp = os.path.join(obj, "link.rsp")
    args = (["-std=c++20", march, host_opt] + lift_objs + ppu_objs + extra + [nid_obj] + host_objs + spu_objs
            + render_objs + [runtime_lib] +
            ["-static", "-Wl,--stack,33554432", "-Wl,--disable-dynamicbase,--disable-high-entropy-va",
             "-ld3d12", "-ldxgi", "-ld3dcompiler", "-ldxguid", "-luuid", "-lntdll", "-lwinmm", "-lxinput1_4",
             "-lbcrypt", "-lole32", "-luser32", "-lgdi32", "-lws2_32", "-lshlwapi", "-lwinpthread", "-lm",
             "-o", out_exe])
    with open(rsp, "w", encoding="utf-8") as f:
        f.write("\n".join('"%s"' % a.replace("\\", "/") for a in args) + "\n")
    tmp_out = out_exe
    r = subprocess.run([tc.cxx, "@" + rsp], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    out = r.stdout.decode("utf-8", "replace")
    with open(log, "a", encoding="utf-8") as f:
        f.write("$ link\n%s\n" % out)
    if r.returncode or not os.path.isfile(tmp_out):
        und = [l for l in out.splitlines() if "undefined" in l or "error" in l]
        raise BuildError("link failed:\n" + "\n".join(und[:30] or out.splitlines()[-30:]))
