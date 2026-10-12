/* included (-include) into every d3d12_up translation unit; see wp_getenv_cached in win_compat.c */
#ifndef WP_GETENV_H
#define WP_GETENV_H
char* wp_getenv_cached(const char* name);
#endif
