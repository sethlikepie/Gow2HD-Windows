/*
 * ps3recomp - backend nativo de input macOS via GameController framework.
 *
 * Substitui o shim SDL_GameController no macOS (regra Port Apple) por
 * GCController (DualShock 4/DualSense/Xbox/MFi nativos + haptics), GCKeyboard
 * e GCMouse -- teclado e mouse mapeados para o pad do PS3 estilo GoW
 * (WASD = stick esquerdo, mouse = stick direito, botoes = ataques). API C
 * (o .m e' Objective-C; cellPad.c continua C).
 */
#ifndef PS3RECOMP_CELLPAD_GAMECONTROLLER_H
#define PS3RECOMP_CELLPAD_GAMECONTROLLER_H

#include "cellpad_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Regista notificacoes de connect/disconnect + descobre teclado/mouse. */
void cpgc_init(void);
void cpgc_shutdown(void);

/* Le o estado corrente de todos os controllers + teclado + mouse e preenche
 * states[0..max_ports-1] no formato PS3. Porto 0 recebe (controller 0) OU
 * teclado+mouse quando nao ha controller ligado nesse porto. */
void cpgc_poll(PadHostState* states, int max_ports);

/* Rumble no porto (motores 0..1 normalizados). */
void cpgc_rumble(int port, float motor_large, float motor_small);

void cpgc_ui_tick(float intensity, float sharpness); /* UI haptic transient on port 0, async */

#ifdef __cplusplus
}
#endif
#endif
