extern "C" void ps3_type15_note_resolve(uint32_t idx, uint32_t obj, uint32_t vt);
extern "C" int ps3_type15_repair_if_needed(uint32_t obj);

/* MENUPRESENT-PROBE extern (defs in ppu_recomp_000.cpp) */
void ps3_mp_on_enter(int id, ppu_context* ctx);
/* SCHEDARM-PROBE extern (defs in ppu_recomp_000.cpp) */
void ps3_sa_on_enter(int id, ppu_context* ctx);
/* POSTTHR-PROBE extern (defs in ppu_recomp_000.cpp) */
void ps3_pt_on_enter(int id, ppu_context* ctx);
void ps3_pt_mark_thr_end(void);
extern "C" volatile int g_ps3_postthr;
/* BCTR-TAIL: despacho de `bctr` (salto, SEM link). Ver
 * recomp_mid_v2/patch_bctr_tail.py e ps3_indirect_tail em
 * ps3recomp/runtime/ppu/ppu_loader.cpp. */
extern "C" void ps3_indirect_tail(ppu_context* ctx);
extern "C" void ps3_call_opd(ppu_context* ctx, uint32_t opd_ea);
