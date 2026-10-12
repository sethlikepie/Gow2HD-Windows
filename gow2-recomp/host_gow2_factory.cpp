/* host_gow2_factory.cpp — subsistema host de factory/TYPE15 do GoW2.
 *
 * PORQUE ESTE FICHEIRO EXISTE
 * ---------------------------
 * Estas ~780 linhas viviam DENTRO do lift gerado (recomp_macos_v2/
 * ppu_recomp_001.cpp), que e' gitignored e regeneravel. Ou seja: codigo host
 * escrito a' mao, sem copia em repositorio nenhum, que desaparecia sem aviso
 * ao apagar ou re-gerar o lift. Medido a 2026-07-24: 9 funcoes (familias
 * ps3_factory_* e ps3_type15_*) existiam SO' ali -- as restantes ps3_* que o
 * lift usa ja' tem casa no motor (runtime/ do ps3recomp).
 *
 * A regra 4 do CLAUDE.md exige que todo o fix ao lift sobreviva como artefacto
 * versionado. Extrair para aqui cumpre isso e torna o comportamento
 * reproduzivel apos um re-lift, em vez de depender de um directorio de 1.5G
 * que nao esta em git.
 *
 * NOTA DE INTEGRACAO: extraido literalmente do lift em producao (mesma
 * semantica, sem reescrita). Ainda NAO esta ligado ao build -- o lift actual
 * continua a trazer a sua propria copia. Ligar (compilar este ficheiro e
 * deixar no lift apenas as declaracoes extern "C") e' o passo seguinte, e
 * exige remover o bloco do lift para nao haver simbolo duplicado.
 *
 * E' codigo especifico do GoW2 (objectos factory/TYPE15 do titulo), por isso
 * vive no checkout do jogo e nao no runtime generico do motor -- ver a meta da
 * Fase 12 do plano de fiabilidade: "o runtime generico nao contem enderecos ou
 * FSMs especificas de um jogo".
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Primitivas de memoria guest, definidas no runtime do motor (ppu_loader). */
extern "C" uint8_t  vm_read8 (uint64_t a);
extern "C" uint16_t vm_read16(uint64_t a);
extern "C" uint32_t vm_read32(uint64_t a);
extern "C" void     vm_write16(uint64_t a, uint16_t v);
extern "C" void     vm_write32(uint64_t a, uint32_t v);
extern "C" uint8_t* vm_base;

/* ---- Type factory 0x15 (SNDX / table idx 0x54 @ 0x868D48) ----
 * Root cause (2026-07-22): freelist FREE of the live factory object
/* ---- Type factory 0x15 (SNDX / table idx 0x54 @ 0x868D48) ----
 * Root cause (2026-07-22): freelist FREE of the live factory object
 * (func_00263318) then re-alloc as stream/string buffer → *obj becomes
 * ASCII 0x5F436F75 ("_Cou" from R_Perm "_Count…"). Table slot still points
 * at the same EA. Fix: PIN free of the snapped object; block non-live
 * stores to word0 via ps3_type15_block_stomp (vm_write32). REPAIR remains
 * as defensive last-resort only (should stay 0 with pin). */
static uint32_t g_ty15_obj = 0;
static uint32_t g_ty15_vt = 0;
static int g_ty15_snap = 0;

static int ty15_vt_looks_live(uint32_t vt) {
    /* Guest .text/.data OPDs live below ~0x600000 for this EBOOT. */
    return vt >= 0x10000u && vt < 0x00600000u;
}

/* General factory vt snap: stream stomp also hits non-TYPE15 factories
 * (e.g. B71 icallA ent=0x400D6808 was vt=0x515700 → 0x02000000).
 * Was 32 — WAD constructs >32 unique factories before B71, so 0x400D6808
 * never entered the table and repair silently failed (vt stayed 0x02000000). */
#define PS3_FACT_SNAP_MAX 128
static uint32_t g_fact_snap_obj[PS3_FACT_SNAP_MAX];
static uint32_t g_fact_snap_vt[PS3_FACT_SNAP_MAX];
static uint32_t g_fact_snap_p48[PS3_FACT_SNAP_MAX]; /* last good product list hdr */
static uint32_t g_fact_snap_prod[PS3_FACT_SNAP_MAX]; /* last good product ptr */
static int g_fact_snap_n = 0;
static int g_fact_snap_clock = 0;
static int g_fact_snap_lru[PS3_FACT_SNAP_MAX];

static int fact_snap_idx(uint32_t obj) {
    for (int i = 0; i < g_fact_snap_n; i++)
        if (g_fact_snap_obj[i] == obj) return i;
    return -1;
}

extern "C" void ps3_factory_snap_vt(uint32_t obj, uint32_t vt) {
    if (!obj || !ty15_vt_looks_live(vt)) return;
    int i = fact_snap_idx(obj);
    if (i >= 0) {
        g_fact_snap_vt[i] = vt;
        g_fact_snap_lru[i] = ++g_fact_snap_clock;
        return;
    }
    if (g_fact_snap_n < PS3_FACT_SNAP_MAX) {
        i = g_fact_snap_n++;
        g_fact_snap_obj[i] = obj;
        g_fact_snap_vt[i] = vt;
        g_fact_snap_p48[i] = 0;
        g_fact_snap_prod[i] = 0;
        g_fact_snap_lru[i] = ++g_fact_snap_clock;
        return;
    }
    /* Table full: replace least-recently used slot (keep TYPE15 pin sticky). */
    int victim = 0;
    int best = g_fact_snap_lru[0];
    for (int j = 1; j < PS3_FACT_SNAP_MAX; j++) {
        if (g_fact_snap_obj[j] == 0x47D00000u) continue;
        if (g_fact_snap_lru[j] < best) {
            best = g_fact_snap_lru[j];
            victim = j;
        }
    }
    if (g_fact_snap_obj[victim] == 0x47D00000u) {
        /* all slots pinned somehow — overwrite slot 0 only if not pin */
        victim = 0;
        for (int j = 0; j < PS3_FACT_SNAP_MAX; j++)
            if (g_fact_snap_obj[j] != 0x47D00000u) { victim = j; break; }
    }
    g_fact_snap_obj[victim] = obj;
    g_fact_snap_vt[victim] = vt;
    g_fact_snap_p48[victim] = 0;
    g_fact_snap_prod[victim] = 0;
    g_fact_snap_lru[victim] = ++g_fact_snap_clock;
}

/* After successful construct: remember product + fo+0x48. */
extern "C" void ps3_factory_snap_product(uint32_t fo, uint32_t product) {
    if (!fo || !product) return;
    if (product < 0x10000u || product >= 0x4F000000u) return;
    int i = fact_snap_idx(fo);
    if (i < 0) {
        uint32_t vt = vm_read32(fo);
        if (!ty15_vt_looks_live(vt)) return;
        ps3_factory_snap_vt(fo, vt);
        i = fact_snap_idx(fo);
    }
    if (i < 0) return;
    g_fact_snap_prod[i] = product;
    uint32_t p48 = vm_read32(fo + 0x48u);
    if (p48 >= 0x10000u && p48 < 0x4F000000u)
        g_fact_snap_p48[i] = p48;
    else
        g_fact_snap_p48[i] = product - 4u; /* list hdr convention */
}

extern "C" int ps3_factory_repair_vt(uint32_t obj) {
    if (!obj) return 0;
    uint32_t cur = vm_read32(obj);
    int i = fact_snap_idx(obj);
    int did = 0;
    if (!ty15_vt_looks_live(cur) && i >= 0 && ty15_vt_looks_live(g_fact_snap_vt[i])) {
        vm_write32(obj, g_fact_snap_vt[i]);
        { static int _n=0; if(_n++<16)
            fprintf(stderr,"[FACTORY] REPAIR obj=0x%08X was=0x%08X -> vt=0x%08X\n",
              obj, cur, g_fact_snap_vt[i]); }
        did = 1;
    }
    /* Restore +0x48 product list if stomped */
    if (i >= 0 && g_fact_snap_p48[i]) {
        uint32_t p48 = vm_read32(obj + 0x48u);
        if (p48 < 0x10000u || p48 >= 0x4F000000u ||
            (p48 >= 0x5F000000u && p48 < 0x7F000000u)) {
            vm_write32(obj + 0x48u, g_fact_snap_p48[i]);
            { static int _n=0; if(_n++<16)
                fprintf(stderr,"[FACTORY] REPAIR +48 obj=0x%08X was=0x%08X -> 0x%08X\n",
                  obj, p48, g_fact_snap_p48[i]); }
            did = 1;
        }
    }
    return did;
}


/* product+0x70 is an intrusive circular list (sentinel = product+0x70).
 * func_002A5024 inits both links to self. Reused products often keep a
 * NULL-terminated pool walk (12×0xD8 nodes) that made func_002A4FE4 hang
 * (NULL → poison 0x27182818 forever).
 *
 * M2 (2026-07-23, H1 / CLOSE-PRESERVE): do NOT wipe a valid non-sentinel
 * head. Prefer close-tail preserve (same poison rules as 2A4FE4 CLOSE-TAIL).
 * Empty circular only when head is 0/bad (shells / freelist replenish). */
static int ps3_type15_list_ptr_bad(uint32_t p) {
    if (p == 0u) return 1;
    if (p < 0x10000u || p >= 0x4F000000u) return 1;
    /* poison / non-heap band seen on NULL-terminated pool tails */
    if (p >= 0x20000000u && p < 0x40000000u) return 1;
    return 0;
}
extern "C" void ps3_type15_product_list_reset(uint32_t prod) {
    if (prod < 0x10000u || prod >= 0x4F000000u) return;
    uint32_t sent = prod + 0x70u;
    uint32_t head = vm_read32(sent);
    /* Already empty circular? */
    if (head == sent && vm_read32(sent + 4u) == sent) return;

    /* Head is sentinel but prev stale → just fix prev. */
    if (head == sent) {
        vm_write32(sent + 4u, sent);
        return;
    }

    /* Head invalid → empty circular (shells, freelist template inherit). */
    if (ps3_type15_list_ptr_bad(head)) {
        vm_write32(sent + 0u, sent);
        vm_write32(sent + 4u, sent);
        { static int _n = 0;
          if (_n++ < 16)
            fprintf(stderr,
                    "[TYPE15] product list RESET prod=0x%08X was_head=0x%08X "
                    "-> circular empty (2A4FE4-safe)\n",
                    prod, head);
        }
        return;
    }

    /* Valid head: walk next pointers; close broken tail onto sentinel.
     * Do not empty the whole list (H1 fix — preserve freelist/pool nodes). */
    {
        uint32_t cur = head;
        uint32_t prev = sent;
        uint32_t nodes = 0u;
        const uint32_t k_cap = 65536u;
        for (;;) {
            if (cur == sent) {
                /* Already circular. Ensure sent->prev = last node. */
                if (prev != sent)
                    vm_write32(sent + 4u, prev);
                { static int _n = 0;
                  if (_n++ < 16)
                    fprintf(stderr,
                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "
                            "head=0x%08X closed_at=0x%08X nodes=%u (already circular)\n",
                            prod, head, prev, nodes);
                }
                return;
            }
            if (ps3_type15_list_ptr_bad(cur)) {
                if (prev == sent) {
                    vm_write32(sent + 0u, sent);
                    vm_write32(sent + 4u, sent);
                    { static int _n = 0;
                      if (_n++ < 16)
                        fprintf(stderr,
                                "[TYPE15] product list RESET prod=0x%08X was_head=0x%08X "
                                "-> circular empty (bad mid-walk)\n",
                                prod, head);
                    }
                } else {
                    vm_write32(prev, sent);
                    vm_write32(sent + 4u, prev);
                    { static int _n = 0;
                      if (_n++ < 16)
                        fprintf(stderr,
                                "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "
                                "head=0x%08X closed_at=0x%08X nodes=%u (bad node)\n",
                                prod, head, prev, nodes);
                    }
                }
                return;
            }
            uint32_t nx = vm_read32(cur);
            nodes++;
            if (nodes >= k_cap) {
                vm_write32(cur, sent);
                vm_write32(sent + 4u, cur);
                { static int _n = 0;
                  if (_n++ < 16)
                    fprintf(stderr,
                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "
                            "head=0x%08X closed_at=0x%08X nodes=%u (cap)\n",
                            prod, head, cur, nodes);
                }
                return;
            }
            if (ps3_type15_list_ptr_bad(nx)) {
                /* Close this node onto sentinel (NULL/poison tail). */
                vm_write32(cur, sent);
                vm_write32(sent + 4u, cur);
                { static int _n = 0;
                  if (_n++ < 16)
                    fprintf(stderr,
                            "[TYPE15] product list CLOSE-PRESERVE prod=0x%08X "
                            "head=0x%08X closed_at=0x%08X nodes=%u\n",
                            prod, head, cur, nodes);
                }
                return;
            }
            prev = cur;
            cur = nx;
        }
    }
}

/* If factory+0x48 has a live product list, return product (hdr+4). */
extern "C" uint32_t ps3_factory_reuse_product(uint32_t fo) {
    if (!fo || fo >= 0x4F000000u) return 0;
    ps3_factory_repair_vt(fo);
    uint32_t hdr = vm_read32(fo + 0x48u);
    uint32_t prod = 0;
    if (hdr >= 0x10000u && hdr < 0x4F000000u)
        prod = hdr + 4u;
    /* Fall back to snapped product if +48 still bad */
    if (prod < 0x10000u || prod >= 0x4F000000u) {
        int i = fact_snap_idx(fo);
        if (i >= 0 && g_fact_snap_prod[i]) {
            prod = g_fact_snap_prod[i];
            hdr = g_fact_snap_p48[i] ? g_fact_snap_p48[i] : (prod - 4u);
            vm_write32(fo + 0x48u, hdr);
        }
    }
    if (prod < 0x10000u || prod >= 0x4F000000u) return 0;
    { static int _n=0; if(_n++<16)
        fprintf(stderr,"[FACTORY] reuse product fo=0x%08X hdr=0x%08X prod=0x%08X\n",
          fo, hdr, prod); }
    return prod;
}

/* Replenish freelist at fo+0x24 using a dedicated pin slab per factory.
 * Layout matches TYPE15 success: *FL=FL+0x18, *(FL+0x18)=shell+4, *(FL+4)=1. */
extern "C" int ps3_factory_freelist_replenish(uint32_t fo, uint16_t type_id) {
    if (!fo || fo >= 0x4F000000u) return 0;
    ps3_factory_repair_vt(fo);
    /* Pin slab below Spurs (0x47C04080) and TYPE15 pin (0x47D00000).
     * Was 0x47C00000 — collided with CreateTaskset/spurs and broke early boot. */
    uint32_t slab = 0x47A00000u + ((fo >> 8) & 0x1FFu) * 0x800u;
    if (slab < 0x47A00000u || slab >= 0x47C00000u) slab = 0x47A00000u;
    uint32_t fl = slab;
    uint32_t shell = slab + 0x400u;
    for (uint32_t off = 0; off < 0x800u; off += 4u)
        vm_write32(slab + off, 0u);
    /* Template from existing product if any */
    uint32_t prod = 0, pvt = 0;
    uint32_t hdr = vm_read32(fo + 0x48u);
    if (hdr >= 0x10000u && hdr < 0x4F000000u) {
        prod = hdr + 4u;
        pvt = vm_read32(prod);
        if (!ty15_vt_looks_live(pvt)) {
            for (uint32_t o = 0; o < 0x40u; o += 4u) {
                uint32_t c = vm_read32(prod + o);
                if (ty15_vt_looks_live(c)) { pvt = c; break; }
            }
        }
        if (prod) {
            for (uint32_t o = 0; o < 0x80u; o += 4u)
                vm_write32(shell + o, vm_read32(prod + o));
        }
    }
    if (ty15_vt_looks_live(pvt))
        vm_write32(shell, pvt);
    if (type_id)
        vm_write16(shell + 2u, type_id);
    uint32_t mid = fl + 0x18u;
    vm_write32(fl + 0u, mid);
    vm_write32(fl + 4u, 1u);
    vm_write32(mid, shell + 4u);
    vm_write32(fo + 0x24u, fl);
    { static int _n=0; if(_n++<16)
        fprintf(stderr,"[FACTORY] freelist REPLENISH fo=0x%08X fl=0x%08X shell=0x%08X type=0x%04X pvt=0x%08X\n",
          fo, fl, shell, type_id, pvt); }
    return 1;
}

extern "C" uint32_t ps3_type15_protected_obj(void) {
    return (g_ty15_snap && g_ty15_obj) ? g_ty15_obj : 0u;
}
extern "C" uint32_t ps3_type15_good_vt(void) {
    return g_ty15_vt;
}
/* Called from vm_write32: refuse stomping factory word0 with non-live data. */
extern "C" int ps3_type15_block_stomp(uint32_t addr, uint32_t val) {
    if (!g_ty15_snap || !g_ty15_obj) return 0;
    if (addr != g_ty15_obj) return 0;
    if (ty15_vt_looks_live(val) || val == g_ty15_vt) return 0; /* allow good vt */
    /* Free marks low bit (vt|1) — also refuse; pin free instead. */
    { static int _n=0; if(_n++<32)
        fprintf(stderr,"[TYPE15] STOMP-BLOCK obj=0x%08X would_write=0x%08X (keep vt=0x%08X)\n",
          g_ty15_obj, val, g_ty15_vt); }
    return 1; /* block */
}
/* Free pin: skip freelist free of factory object (func_00263318). */
extern "C" int ps3_type15_pin_free(uint32_t blk) {
    if (!g_ty15_snap || !g_ty15_obj || blk != g_ty15_obj) return 0;
    { static int _n=0; if(_n++<16)
        fprintf(stderr,"[TYPE15] PIN-FREE skip free of factory obj=0x%08X vt=0x%08X\n",
          g_ty15_obj, g_ty15_vt); }
    return 1;
}

/* Re-home factory to a pin zone outside freelist churn (0x40100000 arena).
 * Original obj at ~0x401002F0 is bulk-stomped (memcpy/stream) with "_Cou";
 * table must point at an EA that never receives that body write. */
/* Inside sys_memory window 0x40100000+0x7D00000 → end 0x47E00000; stay below. */
static const uint32_t k_ty15_pin = 0x47D00000u;
static int g_ty15_rehomed = 0;

extern "C" void ps3_type15_note_resolve(uint32_t idx, uint32_t obj, uint32_t vt) {
    if (idx != 0x54u || !obj) return;
    if (ty15_vt_looks_live(vt)) {
        if (!g_ty15_snap || (g_ty15_obj != obj && g_ty15_obj != k_ty15_pin)) {
            g_ty15_obj = obj;
            g_ty15_vt = vt;
            g_ty15_snap = 1;
            { static int _n=0; if(_n++<8)
                fprintf(stderr,"[TYPE15] SNAP obj=0x%08X vt=0x%08X\n", obj, vt); }
            /* REHOME once: copy object bytes to pin zone, retarget table slot.
             * Also rehome freelist at +0x24 — it lives next to the original
             * factory in 0x401xxxxx and gets stream-stomped to ASCII
             * ("_Capacity…") after first construct, so CB56C 2nd construct
             * sees slot_val=0. Copy freelist blob to pin+0x400 and retarget. */
            if (!g_ty15_rehomed && obj != k_ty15_pin && obj >= 0x40000000u) {
                uint32_t tab = 0x00868D48u;
                const uint32_t k_fl_pin = k_ty15_pin + 0x400u;
                /* Copy ~0x200 of factory object (covers +0xD4/+0xC8 tables). */
                for (uint32_t off = 0; off < 0x200u; off += 4u)
                    vm_write32(k_ty15_pin + off, vm_read32(obj + off));
                /* Ensure live vt on pin. */
                vm_write32(k_ty15_pin, vt);
                /* Freelist rehome: +0x24 points into stomped arena. */
                {
                    uint32_t fl = vm_read32(k_ty15_pin + 0x24u);
                    if (fl >= 0x40000000u && fl < 0x47D00000u) {
                        for (uint32_t off = 0; off < 0x200u; off += 4u)
                            vm_write32(k_fl_pin + off, vm_read32(fl + off));
                        /* TYPE15-FL-REHOME-FIXUP (2026-07-31): o copy acima e'
                         * RAW -- qualquer ponteiro AUTO-REFERENTE dentro do
                         * proprio blob (ex.: o head em fl+0 que apontava para
                         * fl+0x18, o "mid" da mesma free-list) continua a
                         * apontar para dentro da arena ANTIGA (nao protegida),
                         * mesmo depois deste REHOME. Medido 3/3 corridas
                         * (notes/2026-07-31-*): a 2a chamada da fabrica le
                         * esse ponteiro auto-referente -- que entretanto foi
                         * stream-stomped para ASCII ("Orbs", 0x4F726273) por
                         * outra alocacao do guest reusando a arena antiga --
                         * e devolve 'Orbo' (0x4F72626F) em vez de um produto
                         * valido. O reparador (ps3_type15_freelist_replenish)
                         * ate' dispara, mas so' no EPILOGO da chamada seguinte
                         * -- tarde de mais para essa mesma chamada, que ja'
                         * leu o lixo. Traduz qualquer palavra copiada que
                         * caia dentro de [fl, fl+0x200) para o mesmo
                         * deslocamento dentro de [k_fl_pin, k_fl_pin+0x200):
                         * so' assim o blob copiado fica inteiramente
                         * auto-contido na zona pinada, tal como a estrutura
                         * que ps3_type15_freelist_replenish constroi de raiz
                         * mais abaixo neste ficheiro. */
                        for (uint32_t off = 0; off < 0x200u; off += 4u) {
                            uint32_t w = vm_read32(k_fl_pin + off);
                            if (w >= fl && w < fl + 0x200u)
                                vm_write32(k_fl_pin + off, k_fl_pin + (w - fl));
                        }
                        /* TYPE15-FL-SHELL-REHOME (2026-07-31): a traducao acima so'
                         * protege ponteiros AUTO-REFERENTES dentro do proprio
                         * blob [fl, fl+0x200). O "mid" (fl+0x18) guarda um
                         * ponteiro para um OBJECTO SEPARADO (o "shell"; slot =
                         * shell+4) que fica FORA dessa janela e nunca foi pinado.
                         * Medido (PS3_TRACE_TYPE15_SHELL=1): esse objecto
                         * sobrevive a' 1a chamada (produto valido) mas e'
                         * stream-stomped por outra alocacao do guest (uma
                         * tabela de paths tipo "S2_446a/...") antes da 2a
                         * chamada, que devolve NULL. Copia o objecto real
                         * (preserva o conteudo capturado, nao forja um novo)
                         * para o mesmo slot que ps3_type15_freelist_replenish
                         * usa para shells sinteticos e retarget o slot. */
                        {
                            uint32_t mid_val = vm_read32(k_fl_pin + 0x18u);
                            if (mid_val >= 0x10000u && mid_val < 0x4F000000u &&
                                (mid_val < k_ty15_pin || mid_val >= k_ty15_pin + 0x1000u)) {
                                uint32_t old_shell = mid_val - 4u;
                                uint32_t new_shell = k_ty15_pin + 0x800u;
                                for (uint32_t o = 0; o < 0x80u; o += 4u)
                                    vm_write32(new_shell + o, vm_read32(old_shell + o));
                                vm_write32(k_fl_pin + 0x18u, new_shell + 4u);
                                { static int _n=0; if(_n++<8)
                                    fprintf(stderr,"[TYPE15] REHOME shell old=0x%08X pin=0x%08X vt=0x%08X\n",
                                      old_shell, new_shell, vm_read32(new_shell)); }
                            }
                        }
                        vm_write32(k_ty15_pin + 0x24u, k_fl_pin);
                        { static int _n=0; if(_n++<8) {
                            fprintf(stderr,"[TYPE15] REHOME freelist old=0x%08X pin=0x%08X "
                              "*fl=0x%08X fl+4=0x%08X\n",
                              fl, k_fl_pin, vm_read32(k_fl_pin),
                              vm_read32(k_fl_pin + 4u));
                            fprintf(stderr,"[TYPE15] freelist words:");
                            for (uint32_t i=0;i<16;i++)
                              fprintf(stderr," %08X", vm_read32(k_fl_pin + i*4u));
                            fprintf(stderr,"\n");
                          } }
                    }
                }
                /* Table idx 0x54 → pin. */
                if (vm_read32(tab + 0x54u) == obj)
                    vm_write32(tab + 0x54u, k_ty15_pin);
                g_ty15_obj = k_ty15_pin;
                g_ty15_rehomed = 1;
                { static int _n=0; if(_n++<8)
                    fprintf(stderr,"[TYPE15] REHOME old=0x%08X pin=0x%08X tab[0x54]=0x%08X vt=0x%08X "
                      "+24=0x%08X +44=0x%08X +48=0x%08X +C8b=%d +D4=0x%08X\n",
                      obj, k_ty15_pin, vm_read32(tab + 0x54u), vt,
                      vm_read32(k_ty15_pin+0x24), vm_read32(k_ty15_pin+0x44),
                      vm_read32(k_ty15_pin+0x48),
                      (int)(int8_t)vm_read8(k_ty15_pin+0xC8),
                      vm_read32(k_ty15_pin+0xD4)); }
            }
        }
        return;
    }
    /* Defensive only if rehome/pin failed and table still points at stomped EA. */
    if (g_ty15_snap && g_ty15_vt) {
        uint32_t ent = obj;
        uint32_t was = vm_read32(ent);
        if (!ty15_vt_looks_live(was) && g_ty15_rehomed) {
            /* Prefer retarget table to pin rather than REPAIR stomped word. */
            uint32_t tab = 0x00868D48u;
            if (vm_read32(tab + 0x54u) == ent) {
                vm_write32(tab + 0x54u, k_ty15_pin);
                { static int _n=0; if(_n++<16)
                    fprintf(stderr,"[TYPE15] RETARGET tab[0x54] stomped=0x%08X -> pin=0x%08X\n",
                      ent, k_ty15_pin); }
                return;
            }
        }
        if (g_ty15_obj == ent && was != g_ty15_vt) {
            vm_write32(ent, g_ty15_vt);
            { static int _n=0; if(_n++<32)
                fprintf(stderr,"[TYPE15] REPAIR obj=0x%08X was_vt=0x%08X -> vt=0x%08X\n",
                  ent, was, g_ty15_vt); }
        }
    }
}

/* Force freelist into pin zone. Guest construct often rewrites fo+0x24 to
 * 0x401xxxxx after first product; that arena is stream-stomped before CB56C,
 * so slot_val=0 and construct returns null. Always keep FL at pin+0x400. */
static int ty15_force_pin_freelist(void) {
    if (!g_ty15_snap || !g_ty15_rehomed) return 0;
    uint32_t fo = k_ty15_pin;
    uint32_t fl = vm_read32(fo + 0x24u);
    const uint32_t k_fl_pin = k_ty15_pin + 0x400u;
    if (fl >= 0x47D00000u && fl < 0x47E00000u)
        return 0; /* already in pin zone */
    /* Copy whatever is still readable, then retarget. */
    if (fl >= 0x10000u && fl < 0x4F000000u) {
        for (uint32_t off = 0; off < 0x200u; off += 4u)
            vm_write32(k_fl_pin + off, vm_read32(fl + off));
    }
    vm_write32(fo + 0x24u, k_fl_pin);
    { static int _n=0; if(_n++<16)
        fprintf(stderr,"[TYPE15] FORCE freelist pin old=0x%08X -> 0x%08X\n",
          fl, k_fl_pin); }
    return 1;
}

/* Replenish TYPE15 freelist with a fresh shell in pin zone.
 * Original freelist entries live in 0x401xxxxx and get stream-stomped.
 * Layout (from first in-boot success): *FL = FL+0x18, *(FL+0x18) = shell+4, *(FL+4)=count. */
extern "C" int ps3_type15_freelist_replenish(void) {
    if (!g_ty15_snap || !g_ty15_rehomed) return 0;
    ty15_force_pin_freelist();
    uint32_t fo = k_ty15_pin;
    uint32_t fl = vm_read32(fo + 0x24u);
    if (fl < 0x47D00000u || fl >= 0x47E00000u) {
        fl = k_ty15_pin + 0x400u;
        vm_write32(fo + 0x24u, fl);
    }
    uint32_t head = vm_read32(fl);
    uint32_t count = vm_read32(fl + 4u);
    /* Need replenish if count==0 or head/slot look stomped (ASCII 0x5F..) or null. */
    int need = 0;
    if (count == 0u) need = 1;
    else if (head < 0x10000u || head >= 0x4F000000u) need = 1;
    else {
        uint32_t slot = vm_read32(head);
        if (slot < 0x10000u || slot >= 0x4F000000u || (slot >= 0x5F000000u && slot < 0x7F000000u))
            need = 1;
        if (head >= 0x5F000000u && head < 0x7F000000u) need = 1;
    }
    if (!need) return 0;
    const uint32_t shell = k_ty15_pin + 0x800u;
    for (uint32_t off = 0; off < 0x400u; off += 4u)
        vm_write32(shell + off, 0u);
    /* Product-class vt from first live TYPE15 product. +48 is list HEADER
     * (product-4); product object starts at hdr+4. Also stamp type 0x15 at +2
     * so CB56C icall2 indexes factory table correctly. */
    {
        uint32_t pvt = 0;
        uint32_t prod = 0;
        uint32_t hdr = vm_read32(k_ty15_pin + 0x48u);
        if (hdr >= 0x10000u && hdr < 0x4F000000u) {
            prod = hdr + 4u;
            pvt = vm_read32(prod);
            /* If product word0 not a live vt, try scanning +0..0x20 for one. */
            if (!ty15_vt_looks_live(pvt)) {
                for (uint32_t o = 0; o < 0x20u; o += 4u) {
                    uint32_t c = vm_read32(prod + o);
                    if (ty15_vt_looks_live(c)) { pvt = c; break; }
                }
            }
        }
        if (ty15_vt_looks_live(pvt)) {
            vm_write32(shell, pvt);
            /* Copy a few words of product template so construct has state. */
            if (prod) {
                for (uint32_t o = 4; o < 0x40u; o += 4u)
                    vm_write32(shell + o, vm_read32(prod + o));
            }
            /* TYPE15 type id at +2 (CB56C icall2 uses this). */
            vm_write16(shell + 2u, 0x0015u);
            /* Do not inherit a dirty +0x70 list from the template product. */
            ps3_type15_product_list_reset(shell);
        }
        { static int _n=0; if(_n++<8)
            fprintf(stderr,"[TYPE15] shell product vt=0x%08X prod_src=0x%08X +2=0x%04X\n",
              pvt, prod, vm_read16(shell+2u)); }
    }
    uint32_t mid = fl + 0x18u;
    vm_write32(fl + 0u, mid);
    vm_write32(fl + 4u, 1u);
    vm_write32(mid, shell + 4u);
    { static int _n=0; if(_n++<16)
        fprintf(stderr,"[TYPE15] freelist REPLENISH fl=0x%08X mid=0x%08X shell=0x%08X count=1\n",
          fl, mid, shell); }
    return 1;
}

extern "C" int ps3_type15_repair_if_needed(uint32_t obj) {
    if (!g_ty15_snap || !obj) return 0;
    if (g_ty15_obj && obj != g_ty15_obj) return 0;
    /* Always try freelist replenish for TYPE15 pin before construct. */
    if (obj == k_ty15_pin || obj == g_ty15_obj)
        ps3_type15_freelist_replenish();
    uint32_t vt = vm_read32(obj);
    if (ty15_vt_looks_live(vt)) {
        /* Freelist head at +0x24 must not be ASCII/stomped (0x5F... = '_'). */
        uint32_t fl = vm_read32(obj + 0x24u);
        uint32_t head = (fl >= 0x10000u && fl < 0x4F000000u) ? vm_read32(fl) : 0u;
        if (head >= 0x5F000000u && head < 0x7F000000u) {
            /* Prefer pin freelist at pin+0x400 if we rehomed. */
            uint32_t k_fl_pin = k_ty15_pin + 0x400u;
            uint32_t pin_head = vm_read32(k_fl_pin);
            if (pin_head && pin_head < 0x5F000000u) {
                vm_write32(obj + 0x24u, k_fl_pin);
                { static int _n=0; if(_n++<16)
                    fprintf(stderr,"[TYPE15] REPAIR freelist head was_ascii=0x%08X -> pin_fl=0x%08X head=0x%08X\n",
                      head, k_fl_pin, pin_head); }
                return 1;
            }
        }
        return 0;
    }
    if (!g_ty15_vt) return 0;
    vm_write32(obj, g_ty15_vt);
    { static int _n=0; if(_n++<32)
        fprintf(stderr,"[TYPE15] REPAIR-precall obj=0x%08X was=0x%08X -> 0x%08X\n",
          obj, vt, g_ty15_vt); }
    return 1;
}

/* ===================== WAVDRV-PROBE ====================================
 * Probe gated por PS3_TRACE_WAVDRV. Read-only. Ver
 * recomp_mid_v2/patch_wavdrv_probe.py para a cadeia estatica.
 * ====================================================================== */
static int wavdrv_on(void) {
    static int on = -1;
    if (on < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_WAVDRV");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}
static int wavdrv_cstr(uint32_t ea, char* out, int cap) {
    int i;
    out[0] = 0;
    /* Stacks de threads de servico do snd_stream ficam altas (~0xD0000000);
     * o buf do open vive la'. So' excluir a base e a gama de tags HLE. O
     * vm_read8 tem a sua propria guarda de OOB e devolve 0 em falta. */
    if (ea < 0x00010000u || ea >= 0xF0000000u) return 0;
    for (i = 0; i < cap - 1; i++) {
        unsigned c = (unsigned)vm_read8((uint64_t)(ea + (uint32_t)i)) & 0xFFu;
        if (c == 0) break;
        if (c < 0x20u || c > 0x7Eu) { out[i] = '?'; continue; }
        out[i] = (char)c;
    }
    out[i] = 0;
    return i;
}

/* ===================== SND-OPEN-PROBE (Task 1) ==========================
 * Probe gated por PS3_TRACE_SNDOPEN. Estritamente read-only sobre a memoria
 * do guest. Ver recomp_mid_v2/patch_snd_open_probe.py para a cadeia estatica.
 * ====================================================================== */
static int snd_probe_on(void) {
    static int on = -1;
    if (on < 0) {
        extern char* getenv(const char*);
        const char* e = getenv("PS3_TRACE_SNDOPEN");
        on = (e && *e && *e != '0') ? 1 : 0;
    }
    return on;
}

/* EA plausivel de dados do guest: imagem (0x10000..) ate' ao topo das arenas
 * altas observadas em boot (handles FIOS aparecem em 0x43xxxxxx). */
static int snd_probe_ea_ok(uint32_t ea) {
    return ea >= 0x00010000u && ea < 0x4F000000u;
}

/* Le uma C-string ASCII imprimivel; devolve o comprimento (0 se nao parecer
 * texto). Nunca escreve; vm_read8 ja tem guarda de OOB. */
static int snd_probe_cstr(uint32_t ea, char* out, int cap) {
    int i;
    out[0] = 0;
    if (!snd_probe_ea_ok(ea)) return 0;
    for (i = 0; i < cap - 1; i++) {
        unsigned c = (unsigned)vm_read8((uint64_t)(ea + (uint32_t)i)) & 0xFFu;
        if (c == 0) break;
        if (c < 0x20u || c > 0x7Eu) return 0;
        out[i] = (char)c;
    }
    out[i] = 0;
    return i;
}

/* Varre [base, base+len) como u32 e imprime toda a string apontada. E' assim
 * que o caminho do ficheiro aparece sem eu ter de adivinhar o offset. */
static void snd_probe_scan(const char* tag, uint32_t base, uint32_t len) {
    uint32_t off;
    char buf[192];
    if (!snd_probe_ea_ok(base)) return;
    if (snd_probe_cstr(base, buf, (int)sizeof buf) >= 4)
        fprintf(stderr, "[SNDOPEN]   %s inline '%s'\n", tag, buf);
    for (off = 0; off + 4u <= len; off += 4u) {
        uint32_t v = vm_read32((uint64_t)(base + off));
        if (snd_probe_cstr(v, buf, (int)sizeof buf) >= 5)
            fprintf(stderr, "[SNDOPEN]   %s +0x%03X -> 0x%08X '%s'\n", tag, off, v, buf);
    }
}

/* Identifica o ponto de entrada de uma chamada indirecta do guest: o objecto
 * de driver, o OPD em [drv+8] e o par (code, toc) que o bctr vai usar.
 * code >= 0xF0000000 == TAG de import do HLE (ppu_imports.cpp) => funcao do
 * HOST; caso contrario e' uma EA de codigo do proprio guest. */
static void snd_probe_opd(const char* tag, uint32_t drv) {
    uint32_t opd = 0, code = 0, toc = 0;
    if (snd_probe_ea_ok(drv)) {
        opd = vm_read32((uint64_t)(drv + 8u));
        if (snd_probe_ea_ok(opd) || opd >= 0xF0000000u) {
            code = vm_read32((uint64_t)(opd + 0u));
            toc  = vm_read32((uint64_t)(opd + 4u));
        }
    }
    fprintf(stderr, "[SNDOPEN]   %s drv=0x%08X opd=[drv+8]=0x%08X code=0x%08X toc=0x%08X  class=%s\n",
            tag, drv, opd, code, toc,
            drv == 0 ? "DRIVER-NULL (nada instalado)"
                     : (code >= 0xF0000000u ? "HOST/HLE-import-tag"
                                            : (code ? "GUEST-code-EA" : "OPD-VAZIA")));
}

/* MSVC compatibility helpers */
#ifdef _MSC_VER
#include <intrin.h>
/* clang-cl defines _MSC_VER but provides __builtin_clz/clzll natively, so
 * redefining them is a hard error there -- only polyfill for real MSVC. */
#ifndef __clang__
static inline int __builtin_clz(unsigned int x) {
    unsigned long idx;
    _BitScanReverse(&idx, x);
    return 31 - (int)idx;
}
static inline int __builtin_clzll(unsigned long long x) {
    unsigned long idx;
    _BitScanReverse64(&idx, x);
    return 63 - (int)idx;
}
#endif
static inline int64_t ppc_mulhd(int64_t a, int64_t b) {
    int64_t hi;
    _mul128(a, b, &hi);
    return hi;
}
static inline uint64_t ppc_mulhdu(uint64_t a, uint64_t b) {
    uint64_t hi;
    _umul128(a, b, &hi);
    return hi;
}
#else
static inline int64_t ppc_mulhd(int64_t a, int64_t b) {
    return (int64_t)((__int128)(int64_t)a * (__int128)(int64_t)b >> 64);
}
static inline uint64_t ppc_mulhdu(uint64_t a, uint64_t b) {
    return (uint64_t)((unsigned __int128)a * (unsigned __int128)b >> 64);
}
#endif

/* VM base pointer (defined by game project) */
extern "C" uint8_t* vm_base;

/* Real wall-clock PS3 timebase, 79.8 MHz (sys_timer.c) — used by mftb/mftbu. */
extern "C" uint64_t ps3_timebase_now(void);

/* Indirect call dispatch (bctrl/bctr) — implemented by the game project.
 * Looks up the guest address in CTR via a hash table and calls the
 * corresponding host function. Handles OPD resolution. */
