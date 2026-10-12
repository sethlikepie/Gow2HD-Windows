#include "rsx_rt_feedback.h"

int rsx_rt_is_feedback(u32 dest_off, u32 tex_off)
{
    return dest_off != 0u && dest_off == tex_off;
}
