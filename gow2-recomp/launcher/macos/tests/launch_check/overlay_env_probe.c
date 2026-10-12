/* Stand-in for the game in the launcher test: loads PS3_OVERLAY_SETTINGS and
 * prints the fullscreen/VSync rsx_metal_backend_init would choose. */
#include <stdio.h>
#include <stdlib.h>

#include "rsx_overlay_settings.h"

int main(void)
{
    rsx_overlay_settings s;
    const int loaded = rsx_overlay_settings_load(&s, getenv("PS3_OVERLAY_SETTINGS"));
    printf("fullscreen=%d vsync=%d\n",
           rsx_overlay_resolve_flag(getenv("PS3_FULLSCREEN"), loaded, s.fullscreen, 0),
           rsx_overlay_resolve_flag(getenv("PS3_METAL_VSYNC"), loaded, s.vsync, 1));
    return 0;
}
