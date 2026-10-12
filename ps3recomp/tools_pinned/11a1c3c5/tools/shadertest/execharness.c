/*
 * NV40 -> HLSL NUMERICAL validation: runs the translated fragment shader on a
 * real GPU (D3D11 WARP), feeds known inputs via COL0/TEXCOORD varyings, renders
 * one pixel to an R32G32B32A32_FLOAT target, reads it back, and compares to the
 * value computed independently from the NV40 semantics. Proves the arithmetic,
 * not just that the HLSL compiles.
 *
 * Build (mingw):
 *   gcc -O0 -I <ps3>/include execharness.c <ps3>/libs/video/rsx_fp_decompiler.c \
 *       -o execharness.exe -ld3d11 -ld3dcompiler -ldxgi
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>

typedef uint8_t u8; typedef uint32_t u32;
extern int rsx_fp_decompile(const u8*, u32, char*, u32);
extern const char* rsx_fp_opcode_name(u32);

static void put_fp(u8* p, u32 host) {
    u32 be = (host << 16) | (host >> 16);
    p[0]=(u8)(be>>24); p[1]=(u8)(be>>16); p[2]=(u8)(be>>8); p[3]=(u8)be;
}

/* known input values fed to the shader (must mirror the C-side expected math) */
static const float IN_COL0[4] = {0.25f, 0.5f, 0.75f, 1.0f};
static const float IN_TC0 [4] = {2.0f, 4.0f, 6.0f, 8.0f};
static const float IN_TC1 [4] = {0.1f, 0.2f, 0.3f, 0.4f};

/* VS feeds a fullscreen triangle and forwards constant varyings from cb0. */
static const char* VS_SRC =
"cbuffer C:register(b0){float4 c0;float4 c1;float4 fg;float4 t0;float4 t1;};\n"
"struct O{float4 position:SV_POSITION;float4 col0:COLOR0;float4 col1:COLOR1;\n"
" float4 fog:FOG;float4 tc0:TEXCOORD0;float4 tc1:TEXCOORD1;float4 tc2:TEXCOORD2;\n"
" float4 tc3:TEXCOORD3;float4 tc4:TEXCOORD4;float4 tc5:TEXCOORD5;float4 tc6:TEXCOORD6;float4 tc7:TEXCOORD7;};\n"
"O main(uint id:SV_VertexID){O o;float2 p=float2((id<<1)&2,id&2);\n"
" o.position=float4(p*2-1,0,1);\n"
" o.col0=c0;o.col1=c1;o.fog=fg;o.tc0=t0;o.tc1=t1;\n"
" o.tc2=0;o.tc3=0;o.tc4=0;o.tc5=0;o.tc6=0;o.tc7=0;return o;}\n";

static ID3D11Device* dev; static ID3D11DeviceContext* ctx;
static ID3D11RenderTargetView* rtv; static ID3D11Texture2D* rt; static ID3D11Texture2D* stg;
static ID3D11VertexShader* vs; static ID3D11Buffer* cb;
static int g_pass=0, g_fail=0;

static int init_d3d(void) {
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0, &fl, 1,
                                   D3D11_SDK_VERSION, &dev, NULL, &ctx);
    if (FAILED(hr)) { printf("D3D11CreateDevice(WARP) failed 0x%08lX\n", hr); return 0; }

    D3D11_TEXTURE2D_DESC td = {0};
    td.Width=1; td.Height=1; td.MipLevels=1; td.ArraySize=1;
    td.Format=DXGI_FORMAT_R32G32B32A32_FLOAT; td.SampleDesc.Count=1;
    td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_RENDER_TARGET;
    if (FAILED(ID3D11Device_CreateTexture2D(dev,&td,NULL,&rt))) { printf("rt failed\n"); return 0; }
    if (FAILED(ID3D11Device_CreateRenderTargetView(dev,(ID3D11Resource*)rt,NULL,&rtv))) { printf("rtv failed\n"); return 0; }
    td.Usage=D3D11_USAGE_STAGING; td.BindFlags=0; td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
    if (FAILED(ID3D11Device_CreateTexture2D(dev,&td,NULL,&stg))) { printf("stg failed\n"); return 0; }

    ID3DBlob* vb=NULL,*er=NULL;
    if (FAILED(D3DCompile(VS_SRC,strlen(VS_SRC),"vs",NULL,NULL,"main","vs_5_0",0,0,&vb,&er))) {
        printf("VS compile failed: %s\n", er?(char*)ID3D10Blob_GetBufferPointer(er):"?"); return 0; }
    ID3D11Device_CreateVertexShader(dev,ID3D10Blob_GetBufferPointer(vb),ID3D10Blob_GetBufferSize(vb),NULL,&vs);
    ID3D10Blob_Release(vb);

    /* No cull: the fullscreen triangle winding is irrelevant to the test. */
    { ID3D11RasterizerState* rs=NULL; D3D11_RASTERIZER_DESC rd={0};
      rd.FillMode=D3D11_FILL_SOLID; rd.CullMode=D3D11_CULL_NONE; rd.DepthClipEnable=TRUE;
      if (SUCCEEDED(ID3D11Device_CreateRasterizerState(dev,&rd,&rs)))
          ID3D11DeviceContext_RSSetState(ctx,rs); }

    float cbdata[5*4];
    memcpy(cbdata+0,IN_COL0,16); memset(cbdata+4,0,16); memset(cbdata+8,0,16);
    memcpy(cbdata+12,IN_TC0,16); memcpy(cbdata+16,IN_TC1,16);
    D3D11_BUFFER_DESC bd={0}; bd.ByteWidth=sizeof cbdata; bd.Usage=D3D11_USAGE_DEFAULT;
    bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA sd={cbdata,0,0};
    if (FAILED(ID3D11Device_CreateBuffer(dev,&bd,&sd,&cb))) { printf("cb failed\n"); return 0; }
    return 1;
}

/* Run one decompiled FP; read the single pixel into out[4]. Returns 1 ok. */
static int run_fp(const char* hlsl, float out[4]) {
    ID3DBlob* pb=NULL,*er=NULL;
    if (FAILED(D3DCompile(hlsl,strlen(hlsl),"fp",NULL,NULL,"main","ps_5_0",0,0,&pb,&er))) {
        printf("  PS compile failed: %s\n", er?(char*)ID3D10Blob_GetBufferPointer(er):"?");
        if (er) ID3D10Blob_Release(er); return 0; }
    ID3D11PixelShader* ps=NULL;
    ID3D11Device_CreatePixelShader(dev,ID3D10Blob_GetBufferPointer(pb),ID3D10Blob_GetBufferSize(pb),NULL,&ps);
    ID3D10Blob_Release(pb);

    D3D11_VIEWPORT vp={0,0,1,1,0,1};
    ID3D11DeviceContext_RSSetViewports(ctx,1,&vp);
    ID3D11DeviceContext_OMSetRenderTargets(ctx,1,&rtv,NULL);
    float clr[4]={-999,-999,-999,-999};
    ID3D11DeviceContext_ClearRenderTargetView(ctx,rtv,clr);
    ID3D11DeviceContext_VSSetShader(ctx,vs,NULL,0);
    ID3D11DeviceContext_VSSetConstantBuffers(ctx,0,1,&cb);
    ID3D11DeviceContext_PSSetShader(ctx,ps,NULL,0);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ID3D11DeviceContext_IASetInputLayout(ctx,NULL);
    ID3D11DeviceContext_Draw(ctx,3,0);

    ID3D11DeviceContext_CopyResource(ctx,(ID3D11Resource*)stg,(ID3D11Resource*)rt);
    D3D11_MAPPED_SUBRESOURCE ms;
    if (FAILED(ID3D11DeviceContext_Map(ctx,(ID3D11Resource*)stg,0,D3D11_MAP_READ,0,&ms))) {
        ID3D11PixelShader_Release(ps); return 0; }
    memcpy(out, ms.pData, 16);
    ID3D11DeviceContext_Unmap(ctx,(ID3D11Resource*)stg,0);
    ID3D11PixelShader_Release(ps);
    return 1;
}

static int close4(const float a[4], const float b[4]) {
    for (int i=0;i<4;i++){ float d=a[i]-b[i]; if (d<0)d=-d; if (d>1e-3f) return 0; } return 1;
}

/* ---- ucode builders ---- */
static u32 SWZ = 0xE4u<<9; /* xyzw */
/* MOV rDst, <input src selected by input_src> */
static void mov_in(u8* p, u32 dst, u32 input_src, int end) {
    u32 w0=(0x01u<<24)|(0xFu<<9)|(dst<<1)|(input_src<<13)|(end?1u:0u);
    put_fp(p+0,w0); put_fp(p+4,(1u/*INPUT*/)|SWZ); put_fp(p+8,SWZ); put_fp(p+12,SWZ);
}
/* arith op rDst = f(r_s0, r_s1, r_s2) with TEMP sources */
static void op_tmp(u8* p, u32 opc, u32 dst, u32 s0, u32 s1, u32 s2, int end) {
    u32 w0=(opc<<24)|(0xFu<<9)|(dst<<1)|(end?1u:0u);
    put_fp(p+0,w0); put_fp(p+4,(s0<<2)|SWZ); put_fp(p+8,(s1<<2)|SWZ); put_fp(p+12,(s2<<2)|SWZ);
}

#define COL0 0x1u
#define TC0  0x4u
#define TC1  0x5u

static void check(const char* name, const u8* uc, u32 sz, const float exp[4]) {
    char hlsl[16384];
    int n = rsx_fp_decompile(uc, sz, hlsl, sizeof hlsl);
    if (n<0) { printf("  FAIL %-8s decompile -1\n", name); g_fail++; return; }
    float got[4];
    if (!run_fp(hlsl, got)) { printf("  FAIL %-8s run\n", name); g_fail++; return; }
    if (close4(got, exp)) { g_pass++; printf("  PASS %-8s (%.3f %.3f %.3f %.3f)\n", name, got[0],got[1],got[2],got[3]); }
    else { g_fail++; printf("  FAIL %-8s got(%.3f %.3f %.3f %.3f) exp(%.3f %.3f %.3f %.3f)\n",
           name, got[0],got[1],got[2],got[3], exp[0],exp[1],exp[2],exp[3]); }
}

int main(void) {
    if (!init_d3d()) return 2;
    u8 uc[128];

    /* MOV r0, col0 */
    mov_in(uc,0,COL0,1);
    check("MOV", uc, 16, IN_COL0);

    /* r1=col0; r2=tc0; ADD r0,r1,r2 */
    { float e[4]; for(int i=0;i<4;i++)e[i]=IN_COL0[i]+IN_TC0[i];
      mov_in(uc,1,COL0,0); mov_in(uc+16,2,TC0,0); op_tmp(uc+32,0x03,0,1,2,0,1);
      check("ADD", uc, 48, e); }

    /* MUL */
    { float e[4]; for(int i=0;i<4;i++)e[i]=IN_COL0[i]*IN_TC0[i];
      mov_in(uc,1,COL0,0); mov_in(uc+16,2,TC0,0); op_tmp(uc+32,0x02,0,1,2,0,1);
      check("MUL", uc, 48, e); }

    /* MAD r0 = col0*tc0 + tc1 */
    { float e[4]; for(int i=0;i<4;i++)e[i]=IN_COL0[i]*IN_TC0[i]+IN_TC1[i];
      mov_in(uc,1,COL0,0); mov_in(uc+16,2,TC0,0); mov_in(uc+32,3,TC1,0);
      op_tmp(uc+48,0x04,0,1,2,3,1);
      check("MAD", uc, 64, e); }

    /* MIN / MAX */
    { float e[4]; for(int i=0;i<4;i++)e[i]=IN_COL0[i]<IN_TC0[i]?IN_COL0[i]:IN_TC0[i];
      mov_in(uc,1,COL0,0); mov_in(uc+16,2,TC0,0); op_tmp(uc+32,0x08,0,1,2,0,1);
      check("MIN", uc, 48, e); }
    { float e[4]; for(int i=0;i<4;i++)e[i]=IN_COL0[i]>IN_TC0[i]?IN_COL0[i]:IN_TC0[i];
      mov_in(uc,1,COL0,0); mov_in(uc+16,2,TC0,0); op_tmp(uc+32,0x09,0,1,2,0,1);
      check("MAX", uc, 48, e); }

    /* RCP r0 = 1/col0.x broadcast */
    { float e[4]; for(int i=0;i<4;i++)e[i]=1.0f/IN_COL0[0];
      mov_in(uc,1,COL0,0); op_tmp(uc+16,0x1A,0,1,0,0,1);
      check("RCP", uc, 32, e); }

    /* FRC r0 = frac(tc0) */
    { float e[4]; for(int i=0;i<4;i++){ float v=IN_TC0[i]; e[i]=v-floorf(v); }
      mov_in(uc,1,TC0,0); op_tmp(uc+16,0x10,0,1,0,0,1);
      check("FRC", uc, 32, e); }

    /* DP4 r0 = dot(col0,tc0) broadcast */
    { float d=0; for(int i=0;i<4;i++)d+=IN_COL0[i]*IN_TC0[i]; float e[4]={d,d,d,d};
      mov_in(uc,1,COL0,0); mov_in(uc+16,2,TC0,0); op_tmp(uc+32,0x06,0,1,2,0,1);
      check("DP4", uc, 48, e); }

    /* DP3 r0 = dot(col0.xyz,tc0.xyz) broadcast */
    { float d=0; for(int i=0;i<3;i++)d+=IN_COL0[i]*IN_TC0[i]; float e[4]={d,d,d,d};
      mov_in(uc,1,COL0,0); mov_in(uc+16,2,TC0,0); op_tmp(uc+32,0x05,0,1,2,0,1);
      check("DP3", uc, 48, e); }

    /* DST r0 = {1, col0.y*tc0.y, col0.z, tc0.w} */
    { float e[4]={1.0f, IN_COL0[1]*IN_TC0[1], IN_COL0[2], IN_TC0[3]};
      mov_in(uc,1,COL0,0); mov_in(uc+16,2,TC0,0); op_tmp(uc+32,0x07,0,1,2,0,1);
      check("DST", uc, 48, e); }

    /* MAD_SAT r0 = saturate(col0*tc0+tc1) */
    { float e[4]; for(int i=0;i<4;i++){ float v=IN_COL0[i]*IN_TC0[i]+IN_TC1[i]; e[i]=v<0?0:(v>1?1:v); }
      mov_in(uc,1,COL0,0); mov_in(uc+16,2,TC0,0); mov_in(uc+32,3,TC1,0);
      { u32 w0=(0x04u<<24)|(0xFu<<9)|(0u<<1)|(1u<<31)|1u;
        put_fp(uc+48,w0); put_fp(uc+52,(1u<<2)|SWZ); put_fp(uc+56,(2u<<2)|SWZ); put_fp(uc+60,(3u<<2)|SWZ); }
      check("MAD_SAT", uc, 64, e); }

    printf("\n=== NUMERIC RESULT: %d pass, %d fail ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
