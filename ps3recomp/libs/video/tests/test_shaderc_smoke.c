/* shaderc links and compiles a trivial Vulkan GLSL fragment shader. */
#include <shaderc/shaderc.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char* src =
        "#version 450\n"
        "layout(location = 0) out vec4 c;\n"
        "void main() { c = vec4(1.0); }\n";
    shaderc_compiler_t cc = shaderc_compiler_initialize();
    shaderc_compile_options_t op = shaderc_compile_options_initialize();
    shaderc_compile_options_set_target_env(op, shaderc_target_env_vulkan,
                                           shaderc_env_version_vulkan_1_2);
    shaderc_compilation_result_t r = shaderc_compile_into_spv(
        cc, src, strlen(src), shaderc_fragment_shader, "smoke.frag", "main", op);
    int ok = shaderc_result_get_compilation_status(r) == shaderc_compilation_status_success;
    if (!ok) printf("FAIL: %s\n", shaderc_result_get_error_message(r));
    else printf("[PASS] shaderc smoke: %zu bytes SPIR-V\n", shaderc_result_get_length(r));
    shaderc_result_release(r);
    shaderc_compile_options_release(op);
    shaderc_compiler_release(cc);
    return ok ? 0 : 1;
}
