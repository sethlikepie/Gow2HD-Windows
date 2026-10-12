/*
 * ps3recomp - VP->VS / FP->PS wire + linkage proof (offline)
 *
 * Proves the Task-2 wire that d3d12_set_shader now performs:
 *   1. rsx_vp_decompile() of a real NV40 vertex program  -> D3DCompile vs_5_0
 *   2. rsx_fp_decompile() of the real PS3_SHADER_DEMO FP  -> D3DCompile ps_5_0
 *   3. The VS output signature is a superset of the PS input signature
 *      (semantic name + index), which is exactly D3D12's VS->PS link rule that
 *      CreateGraphicsPipelineState enforces. Done with D3DReflect so the test
 *      is device-free and deterministic (no GPU/adapter dependency).
 *
 * The FP is the exact microcode injected by cellGcmSys.c's PS3_SHADER_DEMO
 * (TEX r0, TC0, END) — the same program the in-boot pipeline compiles. The VP
 * is a minimal synthetic NV40 program (MOV, D3.end) since GoW2 does not emit a
 * VP at this point in the boot (in-boot the backend correctly falls back to the
 * passthrough VS; this harness exercises the full VP path the boot cannot).
 *
 * Build (mingw):
 *   gcc -std=gnu11 -O0 -I<ps3>/include -I<ps3>/libs/video test_vp_ps_link.c \
 *       ../rsx_vp_decompiler.c ../rsx_fp_decompiler.c \
 *       -ld3dcompiler -ldxguid -o test_vp_ps_link.exe
 */
#include "rsx_vp_decompiler.h"
#include "rsx_fp_decompiler.h"
#include <stdio.h>
#include <string.h>

/* Windows-only harness (task F0-4: varredura de portabilidade dos testes):
 * usa D3DCompile/D3DReflect (mingw) para provar a ligacao VS->PS -- sem
 * porte POSIX (precisaria de um compilador HLSL alternativo). Nao e
 * invocado por nenhum script/CMake deste repo; validate_vp_shaders.c e
 * validate_fp_shaders.c (mesmo directorio) ja gateiam o D3DCompile com
 * #ifdef _WIN32 e continuam uteis fora do Windows -- este ficheiro nao.
 */
#ifndef _WIN32
#error test_vp_ps_link e um harness so-Windows (D3DCompile/D3DReflect via mingw); sem porte POSIX aqui.
#endif

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3dcompiler.h>
#include <d3d11shader.h>

/* Store a logical host-order FP word so rsx_fp_read_word() returns it (it does a
 * 16-bit half-word swap over a big-endian load). Mirrors cellGcmSys.c put. */
static void put_fp(unsigned char* p, unsigned int host) {
    unsigned int be = (host << 16) | (host >> 16);
    p[0] = (unsigned char)(be >> 24); p[1] = (unsigned char)(be >> 16);
    p[2] = (unsigned char)(be >> 8);  p[3] = (unsigned char)be;
}
/* Store a VP word little-endian (RPCS3 native order, what rsx_vp_decompile reads). */
static void put_vp(unsigned char* p, unsigned int w) {
    p[0] = (unsigned char)w; p[1] = (unsigned char)(w >> 8);
    p[2] = (unsigned char)(w >> 16); p[3] = (unsigned char)(w >> 24);
}

static char vs_hlsl[64 * 1024];
static char ps_hlsl[64 * 1024];

/* Reflect a blob's signature parameters into a caller array of {name,index}. */
typedef struct { char name[32]; unsigned int index; } SigParam;

static int reflect_sig(ID3DBlob* blob, int want_output, SigParam* out, int max_out)
{
    ID3D11ShaderReflection* refl = NULL;
    HRESULT hr = D3DReflect(blob->lpVtbl->GetBufferPointer(blob),
                            blob->lpVtbl->GetBufferSize(blob),
                            &IID_ID3D11ShaderReflection, (void**)&refl);
    if (FAILED(hr) || !refl) { printf("  D3DReflect failed (0x%08lX)\n", hr); return -1; }

    D3D11_SHADER_DESC sd; memset(&sd, 0, sizeof sd);
    refl->lpVtbl->GetDesc(refl, &sd);
    unsigned int n = want_output ? sd.OutputParameters : sd.InputParameters;
    int cnt = 0;
    for (unsigned int i = 0; i < n && cnt < max_out; i++) {
        D3D11_SIGNATURE_PARAMETER_DESC pd; memset(&pd, 0, sizeof pd);
        if (want_output) refl->lpVtbl->GetOutputParameterDesc(refl, i, &pd);
        else             refl->lpVtbl->GetInputParameterDesc(refl, i, &pd);
        /* Skip system-value semantics that don't participate in VS->PS matching
         * by name (they still exist on both sides; keep them, D3D matches them). */
        strncpy(out[cnt].name, pd.SemanticName ? pd.SemanticName : "", 31);
        out[cnt].name[31] = 0;
        out[cnt].index = pd.SemanticIndex;
        cnt++;
    }
    refl->lpVtbl->Release(refl);
    return cnt;
}

int main(void)
{
    /* ---- 1. Synthetic NV40 VP: MOV (vec op 1), NOP scalar, D3.end ---- */
    unsigned char vp[16];
    put_vp(vp + 0, 0u);            /* d0 */
    put_vp(vp + 4, (1u << 22));    /* d1: vec op = MOV(1), sca op = NOP(0) */
    put_vp(vp + 8, 0u);            /* d2 */
    put_vp(vp + 12, 1u);           /* d3: end bit */

    u32 vp_instr = rsx_vp_program_size_instrs(vp, sizeof vp);
    printf("VP: program_size_instrs = %u\n", vp_instr);
    int vr = rsx_vp_decompile(vp, sizeof vp, vs_hlsl, sizeof vs_hlsl);
    printf("VP: rsx_vp_decompile -> %d instr, %u HLSL bytes\n", vr, (unsigned)strlen(vs_hlsl));
    if (vr <= 0) { printf("FAIL: VP decompile\n"); return 1; }

    /* ---- 2. Real PS3_SHADER_DEMO FP: TEX r0, TC0, END ---- */
    unsigned int fpw[4] = {
        (0x17u << 24) | (0xFu << 9) | (0x4u << 13) | (0x0u << 17) | 1u,
        1u | (0u << 9) | (1u << 11) | (2u << 13) | (3u << 15), 0u, 0u
    };
    unsigned char fp[16];
    for (int i = 0; i < 4; i++) put_fp(fp + i * 4, fpw[i]);

    u32 fp_bytes = rsx_fp_program_size(fp, sizeof fp);
    printf("FP: program_size = %u bytes\n", fp_bytes);
    int fr = rsx_fp_decompile(fp, fp_bytes ? fp_bytes : sizeof fp, ps_hlsl, sizeof ps_hlsl);
    printf("FP: rsx_fp_decompile -> %d instr, %u HLSL bytes\n", fr, (unsigned)strlen(ps_hlsl));
    if (fr < 0) { printf("FAIL: FP decompile\n"); return 1; }

    /* ---- 3. Compile VS (vs_5_0) and PS (ps_5_0) ---- */
    ID3DBlob *vsb = NULL, *psb = NULL, *e1 = NULL, *e2 = NULL;
    HRESULT h1 = D3DCompile(vs_hlsl, strlen(vs_hlsl), "vp", NULL, NULL, "main", "vs_5_0", 0, 0, &vsb, &e1);
    printf("VS: D3DCompile vs_5_0 -> %s\n", SUCCEEDED(h1) ? "S_OK" : "FAILED");
    if (FAILED(h1)) { printf("  %s\n", e1 ? (char*)e1->lpVtbl->GetBufferPointer(e1) : "?"); return 1; }
    HRESULT h2 = D3DCompile(ps_hlsl, strlen(ps_hlsl), "fp", NULL, NULL, "main", "ps_5_0", 0, 0, &psb, &e2);
    printf("PS: D3DCompile ps_5_0 -> %s\n", SUCCEEDED(h2) ? "S_OK" : "FAILED");
    if (FAILED(h2)) { printf("  %s\n", e2 ? (char*)e2->lpVtbl->GetBufferPointer(e2) : "?"); return 1; }

    /* ---- 4. Signature linkage: every PS input must have a matching VS output ---- */
    SigParam vout[32], pin[32];
    int nv = reflect_sig(vsb, 1, vout, 32);
    int np = reflect_sig(psb, 0, pin, 32);
    if (nv < 0 || np < 0) { printf("FAIL: reflection\n"); return 1; }
    printf("VS outputs=%d, PS inputs=%d\n", nv, np);

    int missing = 0;
    for (int i = 0; i < np; i++) {
        int found = 0;
        for (int j = 0; j < nv; j++)
            /* HLSL semantic matching is case-insensitive (SV_Position == SV_POSITION),
             * exactly what the D3D12 PSO linker uses. */
            if (!_stricmp(pin[i].name, vout[j].name) && pin[i].index == vout[j].index) { found = 1; break; }
        printf("  PS in  %s%u -> %s\n", pin[i].name, pin[i].index, found ? "matched" : "MISSING in VS out");
        if (!found) missing++;
    }

    vsb->lpVtbl->Release(vsb); psb->lpVtbl->Release(psb);
    if (e1) e1->lpVtbl->Release(e1); if (e2) e2->lpVtbl->Release(e2);

    if (missing) { printf("\nRESULT: FAIL (%d PS inputs unlinked)\n", missing); return 1; }
    printf("\nRESULT: PASS - VP decompiled to vs_5_0 and its VSOutput links every PS input of the FP.\n");
    return 0;
}
