"""movies.py -- fill <out>/movie_cache from the player's gow2.psarc.

Two sets, both read by the engine through PS3_MOVIE_CACHE:
  1. the kit's 17 files (gow2-recomp/kit/movie_cache.sha256: boot movie + sound, intro movie, WADs the host
     video player reads), checked against the kit's hashes -- kit/setup.sh step 3;
  2. every other prerendered movie /_movies/*.m2v (the port plays all 45, WINPORT_ALL_MOVIES) plus the index
     _psarc_movies.txt ("<name> <lo> <hi>" = the member's compressed byte range inside gow2.psarc) that tells
     the engine which movie the game is streaming. Only pictures are extracted: the in-game movies' sound is
     the game's own (SCREAM), so no companion .wav is written for them.
The psarc is parsed with the kit's ps3recomp/tools/psarc_extract.py (header/TOC); members are streamed to disk
block by block. Existing files that already verify are kept (restartable)."""
import hashlib
import importlib.util
import os
import zlib


def _load_psarc(tool):
    spec = importlib.util.spec_from_file_location("psarc_extract", tool)
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


def _member_range(p, ei):
    bidx, usize, off = p.ent[ei]
    nblk = (usize + p.blk - 1) // p.blk
    csz = sum((p.bsz[b] or p.blk) for b in range(bidx, bidx + nblk))
    return off, off + csz, usize


def _extract_to(p, path, dst):
    """Stream one member to dst (same decoding as Psarc._read). Returns (size, sha256)."""
    bidx, usize, off = p.ent[p.idx[path]]
    h = hashlib.sha256()
    tmp = dst + ".part"
    n = 0
    with open(tmp, "wb") as o:
        p.f.seek(off)
        bi = bidx
        while n < usize:
            cs = p.bsz[bi]
            bi += 1
            raw = p.f.read(cs if cs else p.blk)
            if cs:
                try:
                    raw = zlib.decompress(raw)
                except zlib.error:
                    pass   # stored block
            raw = raw[:usize - n]
            o.write(raw)
            h.update(raw)
            n += len(raw)
    os.replace(tmp, dst)
    return n, h.hexdigest()


def _sha(p):
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 22), b""):
            h.update(b)
    return h.hexdigest()


def build_cache(psarc, cache, kit_sha_list, psarc_tool, progress=lambda m: None):
    os.makedirs(cache, exist_ok=True)
    pe = _load_psarc(psarc_tool)
    p = pe.Psarc(psarc)
    lower = {q.lower(): q for q in p.paths}
    # 1. the kit's list
    kit = []
    for l in open(kit_sha_list, encoding="utf-8"):
        if l.strip():
            s, name = l.split(None, 1)
            kit.append((s, name.strip().lstrip("*")))
    for i, (want, name) in enumerate(kit):
        dst = os.path.join(cache, name)
        if os.path.isfile(dst) and _sha(dst) == want:
            continue
        lc = name.lower()
        src = ("/_movies/" if lc.endswith((".m2v", ".wav")) else "/wad/") + lc
        src = lower.get(src, src)
        if src not in p.idx:
            raise ValueError("%s is not in gow2.psarc (different game version?)" % src)
        progress("movie cache %d/%d: %s" % (i + 1, len(kit), name))
        _, got = _extract_to(p, src, dst)
        if got != want:
            raise ValueError("%s does not match gow2-recomp/kit/movie_cache.sha256 (different gow2.psarc?)" % name)
    # 2. every /_movies/*.m2v + the index
    existing = {f.lower() for f in os.listdir(cache)}
    lines, movies = [], [q for q in p.paths if q.lower().startswith("/_movies/") and q.lower().endswith(".m2v")]
    for i, path in enumerate(movies):
        name = path.rsplit("/", 1)[1][:-4]
        lo, hi, usize = _member_range(p, p.idx[path])
        lines.append("%s %d %d" % (name, lo, hi))
        fn = name + ".m2v"
        dst = os.path.join(cache, fn)
        if fn.lower() in existing and (any(k[1].lower() == fn.lower() for k in kit) or os.path.getsize(dst) == usize):
            continue
        progress("movies %d/%d: %s (%.0f MB)" % (i + 1, len(movies), fn, usize / 1048576.0))
        n, _ = _extract_to(p, path, dst)
        if n != usize:
            raise ValueError("short extract of " + path)
    idx = os.path.join(cache, "_psarc_movies.txt")
    with open(idx, "w", newline="\n") as f:
        f.write("# v48: /_movies/*.m2v members of gow2.psarc: name lo hi (compressed byte range, decimal)\n")
        f.write("\n".join(lines) + "\n")
    p.f.close()
    return len(kit), len(movies)
