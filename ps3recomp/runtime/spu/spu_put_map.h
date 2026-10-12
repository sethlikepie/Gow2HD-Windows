/* spu_put_map.h -- see spu_put_map.c (PS3_TRACE_SPU_PUT, diagnostic). */
#ifndef SPU_PUT_MAP_H
#define SPU_PUT_MAP_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned int       ea;
    unsigned int       size;
    int                image_id;   /* which SPU image ran the PUT */
    unsigned int       pc;         /* LS address of the DMA */
    unsigned int       lr;         /* its caller in local store */
    unsigned long long seq;        /* order between transfers */
    int                is_get;     /* 1 = GET (main -> LS), 0 = PUT */
} spu_put_rec;

int  spu_put_map_on(void);
void spu_put_record(unsigned int ea, unsigned int size, int image_id,
                    unsigned int pc, unsigned int lr);
void spu_get_record(unsigned int ea, unsigned int size, int image_id,
                    unsigned int pc, unsigned int lr);
/* Top regions touched, coalesced by 64 KB: who reads and writes what. */
void spu_put_map_summary(void);
/* 1 + *out when some recorded PUT covers ea, 0 otherwise. */
int  spu_put_owner(unsigned int ea, spu_put_rec* out);
unsigned long long spu_put_seq(void);

#ifdef __cplusplus
}
#endif
#endif /* SPU_PUT_MAP_H */
