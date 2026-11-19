/* Serialized root signature in a DXBC container (RTS0 chunk), as passed to
 * ID3D12Device::CreateRootSignature / D3D12CreateVersionedRootSignatureDeserializer:
 * parse, convert to every version, re-serialize, re-parse. */
#include "fuzz_common.h"

int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    fuzz_quiet_logs();
    return 0;
}

static const enum vkd3d_root_signature_version versions[] = {
    VKD3D_ROOT_SIGNATURE_VERSION_1_0, VKD3D_ROOT_SIGNATURE_VERSION_1_1, VKD3D_ROOT_SIGNATURE_VERSION_1_2,
};

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct vkd3d_versioned_root_signature_desc desc, conv, reparsed;
    struct vkd3d_shader_code code, blob;
    vkd3d_shader_hash_t hash;
    unsigned int i;

    memset(&code, 0, sizeof(code));
    code.code = data;
    code.size = size;

    (void)vkd3d_shader_contains_root_signature(data, size);

    memset(&desc, 0, sizeof(desc));
    if (vkd3d_shader_parse_root_signature(&code, &desc, &hash) < 0)
        return 0;

    for (i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i)
    {
        memset(&conv, 0, sizeof(conv));
        if (vkd3d_shader_convert_root_signature(&conv, versions[i], &desc) < 0)
            continue;
        memset(&blob, 0, sizeof(blob));
        if (vkd3d_shader_serialize_root_signature(&conv, &blob) >= 0)
        {
            memset(&reparsed, 0, sizeof(reparsed));
            if (vkd3d_shader_parse_root_signature(&blob, &reparsed, &hash) >= 0)
                vkd3d_shader_free_root_signature(&reparsed);
            vkd3d_shader_free_shader_code(&blob);
        }
        vkd3d_shader_free_root_signature(&conv);
    }
    vkd3d_shader_free_root_signature(&desc);
    return 0;
}
