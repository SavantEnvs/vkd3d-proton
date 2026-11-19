/* DXBC/DXIL shader container -> SPIR-V via the public vkd3d_shader_compile_dxbc() API: the path every
 * game-supplied shader takes in CreateGraphicsPipelineState/CreateComputePipelineState.
 * Input: byte 0 selects the pipeline stage the shader is compiled for, the rest is the blob. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    fuzz_quiet_logs();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct vkd3d_shader_compile_arguments args;
    struct vkd3d_shader_interface_info iface;
    struct vkd3d_shader_code dxbc, spirv;
    struct vkd3d_shader_code_debug dbg;

    if (size < 1)
        return -1;
    fuzz_init_interface(&iface, fuzz_stages[data[0] % (sizeof(fuzz_stages) / sizeof(fuzz_stages[0]))]);
    fuzz_init_args(&args);

    memset(&dxbc, 0, sizeof(dxbc));
    memset(&spirv, 0, sizeof(spirv));
    memset(&dbg, 0, sizeof(dbg));
    dxbc.code = data + 1;
    dxbc.size = size - 1;

    if (vkd3d_shader_compile_dxbc(&dxbc, &spirv, &dbg, 0, &iface, &args) >= 0)
    {
        vkd3d_shader_free_shader_code(&spirv);
        vkd3d_shader_free_shader_code_debug(&dbg);
    }
    return 0;
}
