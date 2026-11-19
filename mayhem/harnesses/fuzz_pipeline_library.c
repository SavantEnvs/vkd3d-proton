/* Cached pipeline blobs (libs/vkd3d/cache.c), the one device-backed input surface:
 *   byte 0 even -> ID3D12Device1::CreatePipelineLibrary(blob) + LoadComputePipeline((const WCHAR *)u"fuzz")
 *   byte 0 odd  -> CreateComputePipelineState with D3D12_COMPUTE_PIPELINE_STATE_DESC.CachedPSO = blob
 * The rest of the input is the blob. The d3d12 device runs on a software Vulkan ICD (lavapipe) and is
 * created ONCE in LLVMFuzzerInitialize, kept on purpose (creating a device per input would cost
 * ~100 ms): every input only creates and releases its own library / pipeline objects on it.
 * Built with -DFUZZ_GEN_SEEDS, main() writes one valid seed of each kind for this device instead. */
#define COBJMACROS
#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "vkd3d.h"
#include "vkd3d_shader.h"

/* tests/d3d12_pso_blob.c: cs_dxbc (an empty cs_5_0 [numthreads(1,1,1)] shader). */
static const DWORD cs_dxbc[] = {
    0x43425844, 0x1acc3ad0, 0x71c7b057, 0xc72c4306, 0xf432cb57, 0x00000001,
    0x00000074, 0x00000003, 0x0000002c, 0x0000003c, 0x0000004c, 0x4e475349,
    0x00000008, 0x00000000, 0x00000008, 0x4e47534f, 0x00000008, 0x00000000,
    0x00000008, 0x58454853, 0x00000020, 0x00050050, 0x00000008, 0x0100086a,
    0x0400009b, 0x00000001, 0x00000001, 0x00000001, 0x0100003e
};

static ID3D12Device1 *device;
static ID3D12RootSignature *root_signature;

static int create_device(void)
{
    struct vkd3d_versioned_root_signature_desc rs_desc;
    struct vkd3d_instance_create_info instance_info;
    struct vkd3d_device_create_info device_info;
    struct vkd3d_shader_code rs_blob;
    struct vkd3d_instance *instance;

    void *libvulkan;

    setenv("VKD3D_DEBUG", "none", 0);
    setenv("VKD3D_SHADER_DEBUG", "none", 0);
    /* No on-disk shader cache: vkd3d otherwise keeps a global pipeline cache in the cwd that would carry
     * state between inputs and makes it ignore application blobs. "0" = rely solely on application
     * caches, i.e. exactly the deserialization path fuzzed here. */
    setenv("VKD3D_SHADER_CACHE_PATH", "0", 1);

    if (!(libvulkan = dlopen("libvulkan.so.1", RTLD_NOW)))
        return -1;
    memset(&instance_info, 0, sizeof(instance_info));
    instance_info.pfn_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)dlsym(libvulkan, "vkGetInstanceProcAddr");
    if (!instance_info.pfn_vkGetInstanceProcAddr)
        return -1;
    if (FAILED(vkd3d_create_instance(&instance_info, &instance)))
        return -1;
    memset(&device_info, 0, sizeof(device_info));
    device_info.minimum_feature_level = D3D_FEATURE_LEVEL_11_0;
    device_info.instance = instance;
    if (FAILED(vkd3d_create_device(&device_info, &IID_ID3D12Device1, (void **)&device)))
        return -1;
    vkd3d_instance_decref(instance);

    memset(&rs_desc, 0, sizeof(rs_desc));
    rs_desc.version = VKD3D_ROOT_SIGNATURE_VERSION_1_0;
    memset(&rs_blob, 0, sizeof(rs_blob));
    if (vkd3d_shader_serialize_root_signature(&rs_desc, &rs_blob) < 0)
        return -1;
    if (FAILED(ID3D12Device1_CreateRootSignature(device, 0, rs_blob.code, rs_blob.size,
            &IID_ID3D12RootSignature, (void **)&root_signature)))
        return -1;
    vkd3d_shader_free_shader_code(&rs_blob);
    return 0;
}

static void init_cs_desc(D3D12_COMPUTE_PIPELINE_STATE_DESC *desc, const void *cached, size_t cached_size)
{
    memset(desc, 0, sizeof(*desc));
    desc->pRootSignature = root_signature;
    desc->CS.pShaderBytecode = cs_dxbc;
    desc->CS.BytecodeLength = sizeof(cs_dxbc);
    desc->CachedPSO.pCachedBlob = cached;
    desc->CachedPSO.CachedBlobSizeInBytes = cached_size;
}

#ifndef FUZZ_GEN_SEEDS
int LLVMFuzzerInitialize(int *argc, char ***argv)
{
    (void)argc; (void)argv;
    if (create_device() < 0)
    {
        fprintf(stderr, "fuzz_pipeline_library: no d3d12 device (is a Vulkan ICD such as lavapipe installed?)\n");
        abort();
    }
    return 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
    ID3D12PipelineLibrary *library;
    ID3D12PipelineState *state;

    uint8_t *blob;

    if (size < 1)
        return -1;
    /* Applications hand over heap buffers (a file read, GetCachedBlob, Serialize), which are 8-byte
     * aligned; data + 1 is not, so pass a malloc'd copy, sized exactly so ASan sees any overread. */
    if (!(blob = malloc(size - 1 ? size - 1 : 1)))
        return 0;
    memcpy(blob, data + 1, size - 1);

    if (!(data[0] & 1))
    {
        if (SUCCEEDED(ID3D12Device1_CreatePipelineLibrary(device, blob, size - 1,
                &IID_ID3D12PipelineLibrary, (void **)&library)))
        {
            init_cs_desc(&desc, NULL, 0);
            if (SUCCEEDED(ID3D12PipelineLibrary_LoadComputePipeline(library, (const WCHAR *)u"fuzz", &desc,
                    &IID_ID3D12PipelineState, (void **)&state)))
                ID3D12PipelineState_Release(state);
            (void)ID3D12PipelineLibrary_GetSerializedSize(library);
            ID3D12PipelineLibrary_Release(library);
        }
    }
    else
    {
        init_cs_desc(&desc, blob, size - 1);
        if (SUCCEEDED(ID3D12Device1_CreateComputePipelineState(device, &desc,
                &IID_ID3D12PipelineState, (void **)&state)))
            ID3D12PipelineState_Release(state);
    }
    free(blob);
    return 0;
}
#else
static int write_seed(const char *path, uint8_t mode, const void *blob, size_t size)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    fwrite(&mode, 1, 1, f);
    fwrite(blob, 1, size, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
    ID3D12PipelineLibrary *library;
    ID3D12PipelineState *state;
    ID3DBlob *cached;
    size_t size;
    void *buf;

    if (argc != 3 || create_device() < 0)
        { fprintf(stderr, "seed generator: step 1 failed\n", 1); return 1; }
    init_cs_desc(&desc, NULL, 0);
    if (FAILED(ID3D12Device1_CreateComputePipelineState(device, &desc, &IID_ID3D12PipelineState, (void **)&state)))
        { fprintf(stderr, "seed generator: step 2 failed\n", 2); return 1; }
    if (FAILED(ID3D12PipelineState_GetCachedBlob(state, &cached)))
        { fprintf(stderr, "seed generator: step 3 failed\n", 3); return 1; }
    if (write_seed(argv[2], 1, ID3D10Blob_GetBufferPointer(cached), ID3D10Blob_GetBufferSize(cached)) < 0)
        { fprintf(stderr, "seed generator: step 4 failed\n", 4); return 1; }
    ID3D10Blob_Release(cached);

    if (FAILED(ID3D12Device1_CreatePipelineLibrary(device, NULL, 0, &IID_ID3D12PipelineLibrary, (void **)&library)))
        { fprintf(stderr, "seed generator: step 5 failed\n", 5); return 1; }
    if (FAILED(ID3D12PipelineLibrary_StorePipeline(library, (const WCHAR *)u"fuzz", state)))
        { fprintf(stderr, "seed generator: step 6 failed\n", 6); return 1; }
    size = ID3D12PipelineLibrary_GetSerializedSize(library);
    buf = malloc(size);
    if (FAILED(ID3D12PipelineLibrary_Serialize(library, buf, size)))
        { fprintf(stderr, "seed generator: step 7 failed\n", 7); return 1; }
    if (write_seed(argv[1], 0, buf, size) < 0)
        { fprintf(stderr, "seed generator: step 8 failed\n", 8); return 1; }
    printf("library seed: %zu bytes\n", size);
    return 0;
}
#endif
