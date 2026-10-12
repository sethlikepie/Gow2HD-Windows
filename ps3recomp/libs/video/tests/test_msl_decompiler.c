/*
 * Offline unit test: NV40 FP → MSL (and optional Metal compile on Apple).
 *
 *   cc -std=c11 -I../../../include test_msl_decompiler.c \
 *      ../rsx_fp_decompiler.c -o test_msl
 *   # Apple Metal compile:
 *   cc -std=c11 -I../../../include -x objective-c test_msl_decompiler.c \
 *      ../rsx_fp_decompiler.c -framework Metal -framework Foundation \
 *      -DTEST_MSL_METAL_COMPILE=1 -o test_msl && ./test_msl
 */
#include "../rsx_fp_decompiler.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#if TEST_MSL_METAL_COMPILE
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>
#endif

static void put_word(u8* p, u32 h)
{
    u32 be = (h << 16) | (h >> 16);
    p[0] = (u8)(be >> 24); p[1] = (u8)(be >> 16);
    p[2] = (u8)(be >> 8);  p[3] = (u8)(be);
}

#define OPC(x)      ((u32)(x) << 24)
#define OUTMASK_ALL (0xFu << 9)
#define END         (1u << 0)
#define INSRC(x)    ((u32)(x) << 13)
#define SWZ_IDENT   ((0u<<9)|(1u<<11)|(2u<<13)|(3u<<15))
#define T_INPUT     1u

int main(void)
{
    char msl[16384];
    u8 prog[16];
    /* MOV r0, COL0 END — same as test_fp_decompiler test1 */
    put_word(prog + 0, OPC(0x01) | OUTMASK_ALL | INSRC(1) | END);
    put_word(prog + 4, T_INPUT | SWZ_IDENT);
    put_word(prog + 8, 0);
    put_word(prog + 12, 0);

    int n = rsx_fp_decompile_msl(prog, sizeof(prog), msl, sizeof(msl));
    printf("instr=%d msl_bytes=%zu\n", n, strlen(msl));
    if (n < 1) { printf("FAIL decompile\n"); return 1; }
    if (!strstr(msl, "fs_main")) { printf("FAIL missing fs_main\n%s\n", msl); return 1; }
    if (!strstr(msl, "metal_stdlib")) { printf("FAIL missing metal_stdlib\n"); return 1; }
    if (!strstr(msl, "input.col0")) { printf("FAIL missing input.col0\n%s\n", msl); return 1; }
    if (strstr(msl, "SV_TARGET") || strstr(msl, "SamplerState")) {
        printf("FAIL HLSL residue left in MSL\n%s\n", msl);
        return 1;
    }
    if (!strstr(msl, "return r[0]")) {
        printf("FAIL AUTO export should return r[0] for MOV r0\n%s\n", msl);
        return 1;
    }
    printf("[PASS] MSL shape ok\n");

    /* SET_SHADER_CONTROL bit 0x40 clear → h0 (Claude E467 yellow-cast). */
    rsx_fp_set_color_export(0);
    n = rsx_fp_decompile_msl(prog, sizeof(prog), msl, sizeof(msl));
    rsx_fp_set_color_export(-1);
    if (n < 1 || !strstr(msl, "return h[0]")) {
        printf("FAIL force h0 export\n%s\n", msl);
        return 1;
    }
    rsx_fp_set_color_export(1);
    n = rsx_fp_decompile_msl(prog, sizeof(prog), msl, sizeof(msl));
    rsx_fp_set_color_export(-1);
    if (n < 1 || !strstr(msl, "return r[0]")) {
        printf("FAIL force r0 export\n%s\n", msl);
        return 1;
    }
    printf("[PASS] SHADER_CONTROL r0/h0 export\n");

    /* fp6B2E71EA pattern: colour lands in h0, then r0 is clobbered with
     * scalars. AUTO must return h[0] (the yellow-cast bug was return r[0]). */
    {
        u8 both[32];
#define FP_OUT_HALF (1u << 7)
        put_word(both + 0, OPC(0x01) | OUTMASK_ALL | FP_OUT_HALF | INSRC(1));
        put_word(both + 4, T_INPUT | SWZ_IDENT);
        put_word(both + 8, 0);
        put_word(both + 12, 0);
        put_word(both + 16, OPC(0x01) | (1u << 11) | INSRC(1) | END); /* MOV r0.z */
        put_word(both + 20, T_INPUT | SWZ_IDENT);
        put_word(both + 24, 0);
        put_word(both + 28, 0);
        rsx_fp_set_color_export(-1);
        n = rsx_fp_decompile_msl(both, sizeof(both), msl, sizeof(msl));
        if (n < 2 || !strstr(msl, "h[0]") || !strstr(msl, "return h[0]")) {
            printf("FAIL AUTO h0 colour when r0 also written\n%s\n", msl);
            return 1;
        }
        if (strstr(msl, "return r[0]")) {
            printf("FAIL AUTO still returns r[0] (yellow-cast)\n%s\n", msl);
            return 1;
        }
        printf("[PASS] AUTO h0 when colour in h0 (fp6B2E71EA pattern)\n");
#undef FP_OUT_HALF
    }

    /* SHADER_CONTROL DEPTH_EXPORT: depth is r1.z (RPCS3 GLFragmentProgram).
     * MOV r1.z, COL0 ; MOV r0, COL0 END. Off → float4 return; on → FSOut. */
    char msl_depth[16384];
    {
        u8 dp[32];
        put_word(dp + 0, OPC(0x01) | (1u << 11) | (1u << 1) | INSRC(1)); /* MOV r1.z */
        put_word(dp + 4, T_INPUT | SWZ_IDENT);
        put_word(dp + 8, 0);
        put_word(dp + 12, 0);
        put_word(dp + 16, OPC(0x01) | OUTMASK_ALL | INSRC(1) | END);     /* MOV r0 */
        put_word(dp + 20, T_INPUT | SWZ_IDENT);
        put_word(dp + 24, 0);
        put_word(dp + 28, 0);
        rsx_fp_set_color_export(1);
        n = rsx_fp_decompile_msl(dp, sizeof(dp), msl_depth, sizeof(msl_depth));
        if (n < 2 || strstr(msl_depth, "[[depth(any)]]")) {
            printf("FAIL depth export off must keep float4 fs_main\n%s\n", msl_depth);
            return 1;
        }
        rsx_fp_set_depth_export(1);
        n = rsx_fp_decompile_msl(dp, sizeof(dp), msl_depth, sizeof(msl_depth));
        rsx_fp_set_depth_export(0);
        rsx_fp_set_color_export(-1);
        if (n < 2 || !strstr(msl_depth, "fragment FSOut fs_main") ||
            !strstr(msl_depth, "_fo.depth = r[1].z") || !strstr(msl_depth, "_fo.c0 = r[0]") ||
            strstr(msl_depth, "    return r[0];")) {
            printf("FAIL depth export on\n%s\n", msl_depth);
            return 1;
        }
        /* Program that never writes r1: no depth output even with export on. */
        rsx_fp_set_depth_export(1);
        char msl_nod[16384];
        n = rsx_fp_decompile_msl(prog, sizeof(prog), msl_nod, sizeof(msl_nod));
        rsx_fp_set_depth_export(0);
        if (n < 1 || strstr(msl_nod, "FSOut")) {
            printf("FAIL depth export without r1 write\n%s\n", msl_nod);
            return 1;
        }
        printf("[PASS] DEPTH_EXPORT r1.z -> [[depth(any)]]\n");

        /* MRT (SET_SURFACE_COLOR_TARGET A+B): uma saida por alvo. fp32 exporta
         * r0,r2,r3,r4; fp16 exporta h0,h4,h6,h8 (RPCS3). O GoW2 desenha a cena do
         * menu em A+B e a composicao final le' B -- com uma so' saida B ficava
         * por escrever. */
        char msl_mrt[16384];
        rsx_fp_set_color_export(1);
        rsx_fp_set_mrt_count(2);
        n = rsx_fp_decompile_msl(dp, sizeof(dp), msl_mrt, sizeof(msl_mrt));
        if (n < 2 || !strstr(msl_mrt, "float4 c1 [[color(1)]]") ||
            !strstr(msl_mrt, "_fo.c0 = r[0]") || !strstr(msl_mrt, "_fo.c1 = r[2]") ||
            strstr(msl_mrt, "[[depth(any)]]") || strstr(msl_mrt, "[[color(2)]]")) {
            printf("FAIL MRT 2 alvos fp32\n%s\n", msl_mrt);
            return 1;
        }
        rsx_fp_set_mrt_count(4);
        rsx_fp_set_depth_export(1);
        n = rsx_fp_decompile_msl(dp, sizeof(dp), msl_mrt, sizeof(msl_mrt));
        rsx_fp_set_depth_export(0);
        if (n < 2 || !strstr(msl_mrt, "_fo.c3 = r[4]") || !strstr(msl_mrt, "_fo.c2 = r[3]") ||
            !strstr(msl_mrt, "_fo.depth = r[1].z")) {
            printf("FAIL MRT 4 alvos + profundidade\n%s\n", msl_mrt);
            return 1;
        }
        rsx_fp_set_mrt_count(1);
        rsx_fp_set_color_export(-1);
        n = rsx_fp_decompile_msl(dp, sizeof(dp), msl_mrt, sizeof(msl_mrt));
        if (n < 2 || strstr(msl_mrt, "FSOut")) {
            printf("FAIL MRT 1 alvo tem de manter float4 fs_main\n%s\n", msl_mrt);
            return 1;
        }
        printf("[PASS] MRT: c0..c3 de r0,r2,r3,r4\n");
    }

#if TEST_MSL_METAL_COMPILE
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        {
            NSError* derr = nil;
            id<MTLLibrary> dlib = [dev newLibraryWithSource:@(msl_depth) options:nil error:&derr];
            if (!dlib || ![dlib newFunctionWithName:@"fs_main"]) {
                printf("FAIL Metal compile (depth export): %s\nMSL:\n%s\n",
                       derr ? [[derr localizedDescription] UTF8String] : "?", msl_depth);
                return 1;
            }
            printf("[PASS] Metal compile depth-export FS\n");
        }
        if (!dev) { printf("FAIL no Metal device\n"); return 1; }
        /* Pair with trivial passthrough VS for PSO link check is optional;
         * library compile alone proves dialect. */
        NSError* err = nil;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(msl) options:nil error:&err];
        if (!lib) {
            printf("FAIL Metal compile: %s\nMSL:\n%s\n",
                   err ? [[err localizedDescription] UTF8String] : "?", msl);
            return 1;
        }
        id<MTLFunction> fs = [lib newFunctionWithName:@"fs_main"];
        if (!fs) { printf("FAIL fs_main not found in library\n"); return 1; }
        printf("[PASS] Metal newLibraryWithSource + fs_main\n");
        /* RSX alpha test: function constant 0 true specialises the discard in
         * (the GoW2 Colossus' reflection pass wrote depth where the console
         * discarded). The plain fs_main above is the variant without it. */
        if (!strstr(msl, "rsx_atest(") || !strstr(msl, "[[buffer(1)]]")) {
            printf("FAIL alpha test hook missing\n%s\n", msl);
            return 1;
        }
        MTLFunctionConstantValues* cv = [MTLFunctionConstantValues new];
        const bool on = true;
        [cv setConstantValue:&on type:MTLDataTypeBool atIndex:0];
        NSError* aerr = nil;
        id<MTLFunction> fs_at = [lib newFunctionWithName:@"fs_main" constantValues:cv error:&aerr];
        if (!fs_at) {
            printf("FAIL alpha-test variant: %s\n", aerr ? [[aerr localizedDescription] UTF8String] : "?");
            return 1;
        }
        printf("[PASS] alpha-test FS variant (function constant 0)\n");
    }
#endif
    return 0;
}
