/* Shared helpers for the vkd3d-shader libFuzzer harnesses (mayhem integration). */
#ifndef FUZZ_COMMON_H
#define FUZZ_COMMON_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define VKD3D_DBG_CHANNEL VKD3D_DBG_CHANNEL_API
#include "vkd3d_shader.h"

static const VkShaderStageFlagBits fuzz_stages[] = {
    VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
    VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, VK_SHADER_STAGE_GEOMETRY_BIT,
    VK_SHADER_STAGE_FRAGMENT_BIT, VK_SHADER_STAGE_COMPUTE_BIT,
    VK_SHADER_STAGE_MESH_BIT_EXT, VK_SHADER_STAGE_TASK_BIT_EXT,
};

/* A zeroed-but-valid interface (no bindings), as libs/vkd3d/state.c would pass for an empty root
 * signature. NULL is not accepted by vkd3d_shader_compile_dxil(). */
static void fuzz_init_interface(struct vkd3d_shader_interface_info *iface, VkShaderStageFlagBits stage)
{
    memset(iface, 0, sizeof(*iface));
    iface->min_ssbo_alignment = 16;
    iface->stage = stage;
}

static void fuzz_init_args(struct vkd3d_shader_compile_arguments *args)
{
    memset(args, 0, sizeof(*args));
    args->target = VKD3D_SHADER_TARGET_SPIRV_VULKAN_1_0;
}

/* vkd3d logs to stderr; keep it quiet (read once at first use, the same value for every input). */
static void fuzz_quiet_logs(void)
{
    setenv("VKD3D_DEBUG", "none", 0);
    setenv("VKD3D_SHADER_DEBUG", "none", 0);
}
#endif
