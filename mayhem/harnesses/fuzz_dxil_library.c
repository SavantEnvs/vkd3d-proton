/* DXIL library blob (D3D12_DXIL_LIBRARY_DESC in a raytracing / work-graph state object): RDAT subobject
 * parsing, entry-point enumeration and demangling, global root signature lookup, and per-export
 * compilation of the first entry point -- the path libs/vkd3d/raytracing_pipeline.c drives. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    fuzz_quiet_logs();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct vkd3d_shader_library_entry_point *entries = NULL;
    struct vkd3d_shader_library_subobject *subobjects = NULL;
    size_t entries_size = 0, entries_count = 0, sub_size = 0, sub_count = 0;
    struct vkd3d_shader_interface_local_info local;
    struct vkd3d_shader_compile_arguments args;
    struct vkd3d_shader_interface_info iface;
    struct vkd3d_shader_code code, rs, spirv;
    struct vkd3d_shader_code_debug dbg;
    D3D12_DXIL_LIBRARY_DESC desc;

    memset(&desc, 0, sizeof(desc));
    desc.DXILLibrary.pShaderBytecode = data;
    desc.DXILLibrary.BytecodeLength = size;

    memset(&rs, 0, sizeof(rs));
    (void)vkd3d_shader_dxil_find_global_root_signature_subobject(data, size, &rs);

    if (vkd3d_shader_dxil_append_library_entry_points_and_subobjects(&desc, 0,
            &entries, &entries_size, &entries_count, &subobjects, &sub_size, &sub_count) >= 0
            && entries_count)
    {
        memset(&code, 0, sizeof(code));
        code.code = data;
        code.size = size;
        memset(&spirv, 0, sizeof(spirv));
        memset(&dbg, 0, sizeof(dbg));
        memset(&local, 0, sizeof(local));
        fuzz_init_interface(&iface, entries[0].stage);
        fuzz_init_args(&args);
        if (vkd3d_shader_compile_dxil_export(&code, entries[0].real_entry_point, entries[0].debug_entry_point,
                &spirv, &dbg, &iface, &local, &args) >= 0)
        {
            vkd3d_shader_free_shader_code(&spirv);
            vkd3d_shader_free_shader_code_debug(&dbg);
        }
    }
    vkd3d_shader_dxil_free_library_entry_points(entries, entries_count);
    vkd3d_shader_dxil_free_library_subobjects(subobjects, sub_count);
    return 0;
}
