/* sdl_stubs_headless.c -- SDL2 substituido por stubs, para corridas HEADLESS.
 *
 * Porque existe: o binario com ThreadSanitizer NAO CHEGA AO main(). Medido com
 * `sample` no processo preso -- a pilha da thread principal e'
 *   dyld runInitializers -> dllinit (libSDL2) -> error_dialog
 *     -> -[NSAlert runModal] -> RunCurrentEventLoopInMode -> mach_msg
 * ou seja, o construtor do libSDL2 abre um DIALOGO MODAL de erro do dynapi ao
 * carregar o dylib e fica a' espera de um clique que ninguem da', porque as
 * corridas de medicao sao destacadas. Nao e' incompatibilidade generica:
 * um programa minimo com `-fsanitize=thread -lSDL2` chega ao main e sai limpo.
 * Nao resolvem: DYLD_INSERT_LIBRARIES do runtime do TSan, SDL_VIDEODRIVER=dummy,
 * SDL_DYNAPI_LIBRARY.
 *
 * Como uma corrida de TSan e' headless de qualquer modo (PS3_NO_RSX=1), a saida
 * e' nao linkar o SDL: `SDL_FLAGS="" ... ./build_macos.sh` mais este ficheiro.
 * Tudo devolve falha/vazio, excepto o relogio, que o codigo de FPS usa a serio.
 *
 * NAO entra no binario de jogar -- so' e' linkado quando SDL_FLAGS esta' vazio.
 */
#include <stdint.h>
#include <stddef.h>
#include <time.h>

uint64_t SDL_GetTicks64(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000ull);
}

const char* SDL_GetError(void) { return "SDL stub headless"; }

int  SDL_InitSubSystem(uint32_t flags) { (void)flags; return -1; }
void SDL_QuitSubSystem(uint32_t flags) { (void)flags; }

void* SDL_CreateWindow(const char* t, int x, int y, int w, int h, uint32_t f)
{ (void)t; (void)x; (void)y; (void)w; (void)h; (void)f; return NULL; }
void  SDL_DestroyWindow(void* w) { (void)w; }
uint32_t SDL_GetWindowFlags(void* w) { (void)w; return 0; }
void  SDL_SetWindowTitle(void* w, const char* t) { (void)w; (void)t; }
int   SDL_SetWindowFullscreen(void* w, uint32_t f) { (void)w; (void)f; return -1; }

void* SDL_CreateRenderer(void* w, int i, uint32_t f) { (void)w; (void)i; (void)f; return NULL; }
void  SDL_DestroyRenderer(void* r) { (void)r; }
int   SDL_GetRendererInfo(void* r, void* info) { (void)r; (void)info; return -1; }
int   SDL_GetRendererOutputSize(void* r, int* w, int* h)
{ (void)r; if (w) *w = 0; if (h) *h = 0; return -1; }
int   SDL_RenderClear(void* r) { (void)r; return -1; }
void  SDL_RenderPresent(void* r) { (void)r; }
int   SDL_RenderReadPixels(void* r, const void* rect, uint32_t fmt, void* px, int pitch)
{ (void)r; (void)rect; (void)fmt; (void)px; (void)pitch; return -1; }
int   SDL_SetRenderDrawColor(void* r, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{ (void)r; (void)a; (void)b; (void)c; (void)d; return -1; }

int   SDL_PollEvent(void* ev) { (void)ev; return 0; }

void* SDL_Metal_CreateView(void* w) { (void)w; return NULL; }
void  SDL_Metal_DestroyView(void* v) { (void)v; }
void* SDL_Metal_GetLayer(void* v) { (void)v; return NULL; }
void  SDL_Metal_GetDrawableSize(void* w, int* x, int* y)
{ (void)w; if (x) *x = 0; if (y) *y = 0; }

int   SDL_Vulkan_LoadLibrary(const char* p) { (void)p; return -1; }
void  SDL_Vulkan_UnloadLibrary(void) {}
int   SDL_Vulkan_CreateSurface(void* w, void* inst, void* surf)
{ (void)w; (void)inst; (void)surf; return 0; }
int   SDL_Vulkan_GetInstanceExtensions(void* w, unsigned int* c, const char** names)
{ (void)w; (void)names; if (c) *c = 0; return 0; }
void  SDL_Vulkan_GetDrawableSize(void* w, int* x, int* y)
{ (void)w; if (x) *x = 0; if (y) *y = 0; }
