/*
 * ps3recomp - cellVdec HLE
 *
 * Video decoder: H.264/AVC and MPEG-2 video decoding.
 * Receives AU (access units) from cellDmux and outputs decoded frames.
 */

#ifndef PS3RECOMP_CELL_VDEC_H
#define PS3RECOMP_CELL_VDEC_H

#include "ps3emu/ps3types.h"
#include "ps3emu/error_codes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Error codes
 * -----------------------------------------------------------------------*/
#define CELL_VDEC_ERROR_ARG            0x80610101
#define CELL_VDEC_ERROR_SEQ            0x80610102
#define CELL_VDEC_ERROR_BUSY           0x80610103
#define CELL_VDEC_ERROR_EMPTY          0x80610104
#define CELL_VDEC_ERROR_AU             0x80610105
#define CELL_VDEC_ERROR_PIC            0x80610106
#define CELL_VDEC_ERROR_FATAL          0x80610107

/* ---------------------------------------------------------------------------
 * Codec types
 * -----------------------------------------------------------------------*/
#define CELL_VDEC_CODEC_TYPE_AVC       0x00000001
#define CELL_VDEC_CODEC_TYPE_MPEG2     0x00000002
#define CELL_VDEC_CODEC_TYPE_DIVX      0x00000005

/* Picture format */
#define CELL_VDEC_PIC_FMT_YUV420P     0
#define CELL_VDEC_PIC_FMT_ARGB8888    1

/* Callback message types */
#define CELL_VDEC_MSG_TYPE_AUDONE      0
#define CELL_VDEC_MSG_TYPE_PICOUT      1
#define CELL_VDEC_MSG_TYPE_SEQDONE     2
#define CELL_VDEC_MSG_TYPE_ERROR       3

/* ---------------------------------------------------------------------------
 * Types
 * -----------------------------------------------------------------------*/
typedef u32 CellVdecHandle;

typedef struct CellVdecType {
    u32 codecType;
    u32 profileLevel;
} CellVdecType;

typedef struct CellVdecResource {
    u32 memAddr;
    u32 memSize;
    u32 ppuThreadPrio;
    u32 ppuThreadStackSize;
    u32 spuThreadPrio;
    u32 numOfSpus;
} CellVdecResource;

typedef struct CellVdecAuInfo {
    u32 startAddr;
    u32 size;
    u64 pts;
    u64 dts;
    u64 userData;
} CellVdecAuInfo;

typedef struct CellVdecPicItem {
    u32 codecType;
    u32 startAddr;
    u32 size;
    u32 auNum;
    u64 pts;
    u64 dts;
    u64 userData;
    u32 status;
    u32 picFmt;
    u16 width;
    u16 height;
} CellVdecPicItem;

typedef u32 (*CellVdecCbMsg)(CellVdecHandle handle, u32 msgType,
                               s32 msgData, void* cbArg);

/* ---------------------------------------------------------------------------
 * Functions
 *
 * NOTE: the HLE handlers are defined in cellVdec.c with the runtime dispatch
 * ABI (uint64_t fn(gpr3..gpr10)) rather than the SDK prototypes, because
 * ppu_hle.cpp invokes every handler as fn(gpr3,...,gpr10) and takes the PPC
 * return from the result. Pointer args are guest EAs; structs are big-endian.
 * The SDK-shaped prototypes are intentionally NOT declared here.
 * -----------------------------------------------------------------------*/

/* 1 if this StartSeq should skip guest PICOUT (legacy intro re-Play hang).
 * Default 0: every sequence delivers PICOUT so in-game movies can GetPicture.
 * PS3_VDEC_REPLAY_NOPIC=1 restores skip when startseq_count>=2.
 * PS3_VDEC_REPLAY_PIC=1 forces delivery (old opt-out of the skip). */
int vdec_replay_nopic_skip(int startseq_count);
int vdec_startseq_count(void);

#ifdef __cplusplus
}
#endif

#endif /* PS3RECOMP_CELL_VDEC_H */
