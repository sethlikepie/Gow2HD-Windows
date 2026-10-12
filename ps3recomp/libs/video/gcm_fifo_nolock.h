#ifndef PS3RECOMP_GCM_FIFO_NOLOCK_H
#define PS3RECOMP_GCM_FIFO_NOLOCK_H

/*
 * RSX wake thread walks the command stream without the PPU giant lock.
 * Console RSX consumes get..put concurrently; the walker does not run
 * guest code (s_walking already serializes drains).
 *
 * PS3_GCM_FIFO_NOLOCK=1 walks unlocked. Default OFF: e435nlk (default ON)
 * measured fps_median=22 vs w64=29.
 */
static inline int ps3_gcm_fifo_nolock_parse(const char *e)
{
    if (e && e[0] == '1')
        return 1;
    return 0;
}

#endif
