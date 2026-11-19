/* Raw root-signature payload (no DXBC container / checksum), the input of the upstream
 * programs/vkd3d-rs-parse tool: vkd3d_shader_parse_root_signature_v_1_2_from_raw_payload() plus the
 * layout-compat hash it computes. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    fuzz_quiet_logs();
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct vkd3d_versioned_root_signature_desc desc;
    struct vkd3d_shader_code code;
    vkd3d_shader_hash_t hash = 0;

    memset(&code, 0, sizeof(code));
    code.code = data;
    code.size = size;
    memset(&desc, 0, sizeof(desc));
    if (vkd3d_shader_parse_root_signature_v_1_2_from_raw_payload(&code, &desc, &hash) < 0)
        return 0;
    if (desc.version == VKD3D_ROOT_SIGNATURE_VERSION_1_2)
        hash ^= vkd3d_root_signature_v_1_2_compute_layout_compat_hash(&desc.v_1_2);
    vkd3d_shader_free_root_signature(&desc);
    return 0;
}
