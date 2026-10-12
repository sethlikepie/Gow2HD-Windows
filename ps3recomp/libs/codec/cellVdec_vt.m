/*
 * ps3recomp - cellVdec: descodificador MPEG-2 alimentado pelos AUs do guest (macOS, VideoToolbox).
 *
 * M9 (2026-09-02). Ate' aqui o HLE do cellVdec descodificava o FILME EM CACHE (popen de ffmpeg
 * sobre o .m2v extraido), ignorando os AUs que o jogo entrega em cellVdecDecodeAu. Pelo caminho
 * natural (dearchiver do psarc a funcionar, E138) nao ha' ficheiro em cache -- ha' so' os AUs.
 * Medido com PS3_VDEC_DUMP_AU: AU #0 = sequence header 0x000001B3 (1280x720) + ext + GOP +
 * picture; AUs seguintes = uma picture cada (468..513 bytes). Um VTDecompressionSession MPEG-2
 * com saida kCVPixelFormatType_420YpCbCr8Planar (I420) devolveu 36 frames para 36 AUs.
 *
 * API C (chamada de cellVdec.c):
 *   vdec_vt_open(w,h)          cria a sessao (1 = ok)
 *   vdec_vt_decode(au,n,pts)   submete um AU (assincrono)
 *   vdec_vt_pop(dst,w,h)       copia o frame mais antigo ja' descodificado para I420 em `dst` (1 = havia)
 *   vdec_vt_flush()            espera pelos frames pendentes
 *   vdec_vt_close()            destroi a sessao e larga os frames em fila
 *
 * O VT devolve os frames pela ordem de DESCODIFICACAO (I0 P3 B1 B2 ...). Desde que o overlay
 * saiu (PS3_MOVIE_HLE=0) e' o proprio jogo que mostra estas imagens, e a ordem errada fazia o
 * filme andar para a frente e para tras (medido 2026-09-23 no introhud.m2v: GOP I P B B).
 * Reordenacao classica do MPEG-2: um B sai logo; uma ancora (I/P) fica retida ate' chegar a
 * ancora seguinte. PS3_VDEC_VT_DECODE_ORDER=1 repoe a ordem antiga.
 */
#import <Foundation/Foundation.h>
#import <VideoToolbox/VideoToolbox.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define VT_Q 64
static VTDecompressionSessionRef  s_sess;
static CMVideoFormatDescriptionRef s_fd;
static CVImageBufferRef s_q[VT_Q];
static int s_qh, s_qt;              /* head (pop) / tail (push) */
static pthread_mutex_t s_mtx = PTHREAD_MUTEX_INITIALIZER;
static unsigned s_w, s_h, s_n_in, s_n_out, s_n_err, s_n_drop;

static CVImageBufferRef s_anchor;  /* ultima ancora (I/P) retida para a ordem de exibicao */
static int s_decode_order = -1;

/* caller holds s_mtx; takes ownership of `img` (already retained) */
static void vt_q_push(CVImageBufferRef img)
{
    int nt = (s_qt + 1) % VT_Q;
    if (nt == s_qh) { s_n_drop++; CFRelease(img); return; }   /* fila cheia: descarta */
    s_q[s_qt] = img; s_qt = nt; s_n_out++;
}

static void vt_out_cb(void* rc, void* src, OSStatus st, VTDecodeInfoFlags fl, CVImageBufferRef img, CMTime pts, CMTime dur)
{
    (void)rc; (void)fl; (void)pts; (void)dur;
    if (st != 0 || !img) { s_n_err++; return; }
    unsigned type = (unsigned)(uintptr_t)src;   /* picture_coding_type: 1=I 2=P 3=B, 0=desconhecido */
    pthread_mutex_lock(&s_mtx);
    CVImageBufferRef mine = (CVImageBufferRef)CFRetain(img);
    if (s_decode_order || type == 3u || type == 0u) {
        vt_q_push(mine);
    } else {
        if (s_anchor) vt_q_push(s_anchor);
        s_anchor = mine;
    }
    pthread_mutex_unlock(&s_mtx);
}

/* picture_coding_type do primeiro picture header (00 00 01 00) do AU */
static unsigned vt_pic_type(const uint8_t* au, size_t n)
{
    for (size_t i = 0; i + 5 < n; i++)
        if (au[i] == 0 && au[i + 1] == 0 && au[i + 2] == 1 && au[i + 3] == 0)
            return (au[i + 5] >> 3) & 7u;
    return 0;
}

int vdec_vt_open(unsigned w, unsigned h)
{
    if (s_sess) return 1;
    s_w = w ? w : 1280; s_h = h ? h : 720;
    OSStatus st = CMVideoFormatDescriptionCreate(kCFAllocatorDefault, kCMVideoCodecType_MPEG2Video, (int32_t)s_w, (int32_t)s_h, NULL, &s_fd);
    if (st != 0 || !s_fd) { fprintf(stderr, "[cellVdec-vt] format desc falhou st=%d\n", (int)st); return 0; }
    NSDictionary* attrs = @{ (id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_420YpCbCr8Planar) };
    VTDecompressionOutputCallbackRecord rec = { vt_out_cb, NULL };
    st = VTDecompressionSessionCreate(kCFAllocatorDefault, s_fd, NULL, (__bridge CFDictionaryRef)attrs, &rec, &s_sess);
    if (st != 0 || !s_sess) { fprintf(stderr, "[cellVdec-vt] sessao falhou st=%d\n", (int)st); CFRelease(s_fd); s_fd = NULL; s_sess = NULL; return 0; }
    s_qh = s_qt = 0; s_n_in = s_n_out = s_n_err = s_n_drop = 0;
    if (s_decode_order < 0) { const char* e = getenv("PS3_VDEC_VT_DECODE_ORDER"); s_decode_order = (e && e[0] == '1') ? 1 : 0; }
    fprintf(stderr, "[cellVdec-vt] sessao MPEG-2 %ux%u (I420) aberta; hw=%d\n", s_w, s_h, (int)VTIsHardwareDecodeSupported(kCMVideoCodecType_MPEG2Video));
    return 1;
}

int vdec_vt_decode(const uint8_t* au, size_t n, uint64_t pts90k)
{
    if (!s_sess || !au || !n) return -1;
    CMBlockBufferRef bb = NULL;
    if (CMBlockBufferCreateWithMemoryBlock(kCFAllocatorDefault, NULL, n, kCFAllocatorDefault, NULL, 0, n, 0, &bb) != 0 || !bb) return -2;
    CMBlockBufferReplaceDataBytes(au, bb, 0, n);
    CMSampleTimingInfo ti = { CMTimeMake(3003, 90000), CMTimeMake((int64_t)pts90k, 90000), kCMTimeInvalid };
    CMSampleBufferRef sb = NULL; size_t sz = n;
    if (CMSampleBufferCreateReady(kCFAllocatorDefault, bb, s_fd, 1, 1, &ti, 1, &sz, &sb) != 0 || !sb) { CFRelease(bb); return -3; }
    VTDecodeInfoFlags fo = 0;
    OSStatus ds = VTDecompressionSessionDecodeFrame(s_sess, sb, kVTDecodeFrame_EnableAsynchronousDecompression,
                                                    (void*)(uintptr_t)vt_pic_type(au, n), &fo);
    CFRelease(sb); CFRelease(bb);
    s_n_in++;
    if (ds != 0) { s_n_err++; return (int)ds; }
    return 0;
}

int vdec_vt_pop(uint8_t* dst, unsigned w, unsigned h)
{
    CVImageBufferRef img = NULL;
    pthread_mutex_lock(&s_mtx);
    if (s_qh != s_qt) { img = s_q[s_qh]; s_q[s_qh] = NULL; s_qh = (s_qh + 1) % VT_Q; }
    pthread_mutex_unlock(&s_mtx);
    if (!img) return 0;
    if (dst) {
        CVPixelBufferLockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
        size_t pw = CVPixelBufferGetWidth(img), ph = CVPixelBufferGetHeight(img);
        unsigned cw = (unsigned)(pw < w ? pw : w), ch = (unsigned)(ph < h ? ph : h);
        uint8_t* y = dst; uint8_t* u = dst + (size_t)w * h; uint8_t* vv = u + (size_t)(w / 2) * (h / 2);
        if (CVPixelBufferGetPlaneCount(img) >= 3) {
            const uint8_t* sy = CVPixelBufferGetBaseAddressOfPlane(img, 0); size_t ry = CVPixelBufferGetBytesPerRowOfPlane(img, 0);
            const uint8_t* su = CVPixelBufferGetBaseAddressOfPlane(img, 1); size_t ru = CVPixelBufferGetBytesPerRowOfPlane(img, 1);
            const uint8_t* sv = CVPixelBufferGetBaseAddressOfPlane(img, 2); size_t rv = CVPixelBufferGetBytesPerRowOfPlane(img, 2);
            for (unsigned r = 0; r < ch; r++) memcpy(y + (size_t)r * w, sy + (size_t)r * ry, cw);
            for (unsigned r = 0; r < ch / 2; r++) { memcpy(u + (size_t)r * (w / 2), su + (size_t)r * ru, cw / 2); memcpy(vv + (size_t)r * (w / 2), sv + (size_t)r * rv, cw / 2); }
        }
        CVPixelBufferUnlockBaseAddress(img, kCVPixelBufferLock_ReadOnly);
    }
    CFRelease(img);
    return 1;
}

void vdec_vt_flush(void) { if (s_sess) VTDecompressionSessionWaitForAsynchronousFrames(s_sess); }

int vdec_vt_pending(void) { pthread_mutex_lock(&s_mtx); int n = (s_qt - s_qh + VT_Q) % VT_Q; pthread_mutex_unlock(&s_mtx); return n; }

void vdec_vt_close(void)
{
    if (!s_sess) return;
    VTDecompressionSessionWaitForAsynchronousFrames(s_sess);
    VTDecompressionSessionInvalidate(s_sess); CFRelease(s_sess); s_sess = NULL;
    if (s_fd) { CFRelease(s_fd); s_fd = NULL; }
    pthread_mutex_lock(&s_mtx);
    while (s_qh != s_qt) { if (s_q[s_qh]) CFRelease(s_q[s_qh]); s_q[s_qh] = NULL; s_qh = (s_qh + 1) % VT_Q; }
    if (s_anchor) { CFRelease(s_anchor); s_anchor = NULL; }
    pthread_mutex_unlock(&s_mtx);
    fprintf(stderr, "[cellVdec-vt] fechado: in=%u out=%u err=%u drop=%u\n", s_n_in, s_n_out, s_n_err, s_n_drop);
}
