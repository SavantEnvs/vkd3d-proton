/* Shader I/O signature chunks (ISGN/ISG1/OSGN/OSG1/OSG5/PCSG/PSG1) inside a DXBC container,
 * parsed with the public vkd3d_shader_parse_{input,output,patch_constant}_signature() API. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    fuzz_quiet_logs();
    return 0;
}

static void probe(struct vkd3d_shader_signature *sig)
{
    static const char *const names[] = { "SV_POSITION", "POSITION", "TEXCOORD", "SV_TARGET", "COLOR" };
    unsigned int i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
    {
        (void)vkd3d_shader_find_signature_element(sig, names[i], 0, 0);
        (void)vkd3d_shader_find_signature_element(sig, names[i], 1, 0);
    }
    vkd3d_shader_free_shader_signature(sig);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct vkd3d_shader_signature sig;
    struct vkd3d_shader_code code;

    memset(&code, 0, sizeof(code));
    code.code = data;
    code.size = size;

    memset(&sig, 0, sizeof(sig));
    if (vkd3d_shader_parse_input_signature(&code, &sig) >= 0)
        probe(&sig);
    memset(&sig, 0, sizeof(sig));
    if (vkd3d_shader_parse_output_signature(&code, &sig) >= 0)
        probe(&sig);
    memset(&sig, 0, sizeof(sig));
    if (vkd3d_shader_parse_patch_constant_signature(&code, &sig) >= 0)
        probe(&sig);
    return 0;
}
