==============================================================================
 inventory_lift_markers.py -- Fase 10, inventario antes de codigo
 referencia : /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_macos_v2.pre_v4
 actual     : /Users/andrebrumcortezferreira/Documents/PESSOAL/gow2-recomp/recomp_macos_v2
==============================================================================

-- varredura 1: marcadores presentes em pre_v4, ausentes em v2 --
  CC9D0-LIVE-SKIP                  chunks(ref)=ppu_recomp_000.cpp   candidato(s): patch_cc9d0_live_yield.py, patch_type15_unstick_skip.py
  CC9D0-SKIP                       chunks(ref)=ppu_recomp_000.cpp   candidato(s): patch_cc9d0_live_yield.py, patch_type15_unstick_skip.py
  CC9D0-YIELD                      chunks(ref)=ppu_recomp_000.cpp   candidato(s): patch_cc9d0_live_yield.py
  DONE-HEX                         chunks(ref)=ppu_recomp_001.cpp   SEM patch_*.py candidato -- buraco novo
  F2B-42A118-PROBE                 chunks(ref)=ppu_recomp_005.cpp   SEM patch_*.py candidato -- buraco novo
  F2B-STREAM-SEED                  chunks(ref)=ppu_recomp_001.cpp   candidato(s): patch_f2b_multimb_install.py, patch_fios_f2b_open_block_install.py
  FIOS-CLEAR-PROBE                 chunks(ref)=ppu_recomp_001.cpp, ppu_recomp_002.cpp SEM patch_*.py candidato -- buraco novo
  FIOS-FO-DUMP                     chunks(ref)=ppu_recomp_001.cpp   candidato(s): patch_fios_f2b_fo_block.py
  FIOS-FREELIST-PROBE              chunks(ref)=ppu_recomp_001.cpp   candidato(s): patch_fios_f2b_fo_block.py
  FIOS-MEDIA16C-PROBE              chunks(ref)=ppu_recomp_001.cpp   SEM patch_*.py candidato -- buraco novo
  FIOS-SHUTDOWN-ENTER-002B4894     chunks(ref)=ppu_recomp_001.cpp   SEM patch_*.py candidato -- buraco novo
  FIOS-SHUTDOWN-ENTER-0030E018     chunks(ref)=ppu_recomp_001.cpp   SEM patch_*.py candidato -- buraco novo
  FIOS-SHUTDOWN-ENTER-0030E878     chunks(ref)=ppu_recomp_001.cpp   SEM patch_*.py candidato -- buraco novo
  FO-DUMP                          chunks(ref)=ppu_recomp_001.cpp   candidato(s): patch_fios_f2b_fo_block.py, patch_fios_f2b_fo_ctor.py, patch_fios_f2b_open_block_install.py
  TYPE15-UNSTICK-SKIP              chunks(ref)=ppu_recomp_000.cpp   candidato(s): patch_type15_unstick_skip.py
  total ausentes: 15

-- varredura 2: patches NO-MATCH/UNVERIFIED (excl. PROBE) --
  UNVERIFIED  patch_2b0fb4_opd.py                          classe=FUNCIONAL
  UNVERIFIED  patch_2b3d1c_movie_io.py                     classe=FUNCIONAL
  UNVERIFIED  patch_b71_2f3f0_guard.py                     classe=FUNCIONAL
  UNVERIFIED  patch_b71_cb56c_reuse_block.py               classe=FUNCIONAL
  UNVERIFIED  patch_b71_skip_icallb_reuse.py               classe=FUNCIONAL
  UNVERIFIED  patch_ba808_wadld_eof.py                     classe=FUNCIONAL
  UNVERIFIED  patch_bctr_tail.py                           classe=FUNCIONAL
  UNVERIFIED  patch_ce03c_introseq_block.py                classe=FUNCIONAL
  UNVERIFIED  patch_ce03c_movie_done_reset.py              classe=FUNCIONAL
  UNVERIFIED  patch_f2b_multimb_install.py                 classe=FUNCIONAL
  UNVERIFIED  patch_f2b_multimb_stream.py                  classe=FUNCIONAL
  UNVERIFIED  patch_f2ba9bc_stream_align_install.py        classe=FUNCIONAL
  UNVERIFIED  patch_factory_opd_gate.py                    classe=FUNCIONAL
  UNVERIFIED  patch_factory_snap_ty15_pin.py               classe=FUNCIONAL
  UNVERIFIED  patch_fallthrough_2550c8.py                  classe=FUNCIONAL
  UNVERIFIED  patch_fios_16c_selfheal.py                   classe=FUNCIONAL
  UNVERIFIED  patch_fios_42a1d4_empty.py                   classe=FUNCIONAL
  UNVERIFIED  patch_fios_42b4_cancel_yield.py              classe=FUNCIONAL
  UNVERIFIED  patch_fios_cancel_yield.py                   classe=FUNCIONAL
  UNVERIFIED  patch_fios_done_cancel_yield.py              classe=FUNCIONAL
  UNVERIFIED  patch_fios_done_yield.py                     classe=FUNCIONAL
  UNVERIFIED  patch_fios_f2a_f2b_wad.py                    classe=FUNCIONAL
  UNVERIFIED  patch_fios_f2b_fo_block.py                   classe=FUNCIONAL
  UNVERIFIED  patch_fios_f2b_fo_ctor.py                    classe=FUNCIONAL
  UNVERIFIED  patch_fios_f2b_open_block_install.py         classe=FUNCIONAL
  UNVERIFIED  patch_fios_f2b_open_success.py               classe=FUNCIONAL
  UNVERIFIED  patch_fios_freelist_rebuild.py               classe=FUNCIONAL
  UNVERIFIED  patch_fios_host_pop.py                       classe=FUNCIONAL
  UNVERIFIED  patch_fios_play_already_active.py            classe=FUNCIONAL
  UNVERIFIED  patch_fios_sticky.py                         classe=FUNCIONAL
  UNVERIFIED  patch_fios_stop_yield.py                     classe=FUNCIONAL
  UNVERIFIED  patch_fios_stream_guards_install.py          classe=FUNCIONAL
  UNVERIFIED  patch_fios_stream_pump.py                    classe=FUNCIONAL
  UNVERIFIED  patch_jumptable_2a209c.py                    classe=FUNCIONAL
  UNVERIFIED  patch_jumptable_2b11b8.py                    classe=FUNCIONAL
  UNVERIFIED  patch_st3_audio_done_force.py                classe=FUNCIONAL
  UNVERIFIED  patch_stdlib_getenv.py                       classe=FUNCIONAL
  UNVERIFIED  patch_tydisp_lr.py                           classe=FUNCIONAL
  UNVERIFIED  patch_type1_name.py                          classe=FUNCIONAL
  UNVERIFIED  patch_type1_result.py                        classe=FUNCIONAL
  UNVERIFIED  patch_type15_cb56c_highbit.py                classe=FUNCIONAL
  UNVERIFIED  patch_type15_cb56c_prefer_product_install.py classe=FUNCIONAL
  UNVERIFIED  patch_type15_cb56c_product.py                classe=FUNCIONAL
  UNVERIFIED  patch_type15_cc9d0_base_blocks.py            classe=FUNCIONAL
  UNVERIFIED  patch_type15_list_close_preserve_install.py  classe=FUNCIONAL
  UNVERIFIED  patch_type15_list_preserve.py                classe=FUNCIONAL
  UNVERIFIED  patch_wad_tex_capture.py                     classe=FUNCIONAL
  total: 47

-- varredura 3: patch_*.py sem write_text()/.write()/open(...'w') --
  patch_b71_skip_icallb_reuse.py
  patch_ce03c_pre_play_stop.py
  patch_factory_snap_ty15_pin.py
  patch_fios_f2a_f2b_wad.py
  patch_type15_cb56c_product.py
  total: 5

-- cruzamento: marcador ausente x patch candidato x estado --
  CC9D0-LIVE-SKIP                  -> patch_cc9d0_live_yield.py [aparenta ok -- revalidar]
  CC9D0-LIVE-SKIP                  -> patch_type15_unstick_skip.py [aparenta ok -- revalidar]
  CC9D0-SKIP                       -> patch_cc9d0_live_yield.py [aparenta ok -- revalidar]
  CC9D0-SKIP                       -> patch_type15_unstick_skip.py [aparenta ok -- revalidar]
  CC9D0-YIELD                      -> patch_cc9d0_live_yield.py [aparenta ok -- revalidar]
  DONE-HEX                         -> SEM candidato (buraco novo, precisa de patch_*.py de raiz)
  F2B-42A118-PROBE                 -> SEM candidato (buraco novo, precisa de patch_*.py de raiz)
  F2B-STREAM-SEED                  -> patch_f2b_multimb_install.py [NO-MATCH/UNVERIFIED no gate]
  F2B-STREAM-SEED                  -> patch_fios_f2b_open_block_install.py [NO-MATCH/UNVERIFIED no gate]
  FIOS-CLEAR-PROBE                 -> SEM candidato (buraco novo, precisa de patch_*.py de raiz)
  FIOS-FO-DUMP                     -> patch_fios_f2b_fo_block.py [NO-MATCH/UNVERIFIED no gate]
  FIOS-FREELIST-PROBE              -> patch_fios_f2b_fo_block.py [NO-MATCH/UNVERIFIED no gate]
  FIOS-MEDIA16C-PROBE              -> SEM candidato (buraco novo, precisa de patch_*.py de raiz)
  FIOS-SHUTDOWN-ENTER-002B4894     -> SEM candidato (buraco novo, precisa de patch_*.py de raiz)
  FIOS-SHUTDOWN-ENTER-0030E018     -> SEM candidato (buraco novo, precisa de patch_*.py de raiz)
  FIOS-SHUTDOWN-ENTER-0030E878     -> SEM candidato (buraco novo, precisa de patch_*.py de raiz)
  FO-DUMP                          -> patch_fios_f2b_fo_block.py [NO-MATCH/UNVERIFIED no gate]
  FO-DUMP                          -> patch_fios_f2b_fo_ctor.py [NO-MATCH/UNVERIFIED no gate]
  FO-DUMP                          -> patch_fios_f2b_open_block_install.py [NO-MATCH/UNVERIFIED no gate]
  TYPE15-UNSTICK-SKIP              -> patch_type15_unstick_skip.py [aparenta ok -- revalidar]

==============================================================================
FIM DO INVENTARIO
==============================================================================
