#include "Lapiz/backends/Vulkan/LVK.h"
#include "Lapiz/graphics/Lshader.h"
#include "Lapiz/graphics/Ltexture.h"
#include "Lapiz/core/Lio.h"
#include "Lapiz/core/Lerror.h"
#include "vk_default_shaders.h"

#include <stdlib.h>
#include <string.h>

_Thread_local static char g_vk_shader_error[LAPIZ_SHADER_ERROR_MAX];

/* Vulkan LapizShader: pipeline + pipeline layout. Push constants for colDiffuse at offset 0. */
struct LapizShader {
    VkPipeline pipeline;
    VkPipelineLayout pipeline_layout;
    LapizColor push_constants;       /* colDiffuse at offset 0 */
    int color_offset;                /* 0 for default shader */
    LapizTexture *bound_textures[4]; /* slots 0-3 for SetTextureEx */
};

#define LAPIZ_VK_PUSH_CONSTANT_SIZE (sizeof(LapizColor))

static VkShaderModule create_shader_module(VkDevice device, const void *code, size_t code_size) 
{
    VkShaderModuleCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size,
        .pCode = (const uint32_t *)code,
    };
    VkShaderModule mod;

    if (vkCreateShaderModule(device, &info, NULL, &mod) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    return mod;
}

static VkPipeline create_graphics_pipeline(VkDevice device, VkRenderPass render_pass, VkShaderModule vert_mod, VkShaderModule frag_mod, VkExtent2D extent, VkPipelineLayout *out_layout) 
{
    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = vert_mod,
            .pName = "main",
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = frag_mod,
            .pName = "main",
        },
    };

    VkPipelineVertexInputStateCreateInfo vertex_input = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 0,
        .vertexAttributeDescriptionCount = 0,
    };

    VkPipelineInputAssemblyStateCreateInfo input_assembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
    };

    VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamic_states,
    };

    VkPipelineViewportStateCreateInfo viewport_state = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };

    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .depthClampEnable = VK_FALSE,
        .rasterizerDiscardEnable = VK_FALSE,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .lineWidth = 1.0f,
        .cullMode = VK_CULL_MODE_BACK_BIT,
        .frontFace = VK_FRONT_FACE_CLOCKWISE,
        .depthBiasEnable = VK_FALSE,
    };

    VkPipelineMultisampleStateCreateInfo multisampling = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        .sampleShadingEnable = VK_FALSE,
    };

    VkPipelineColorBlendAttachmentState color_blend_attachment = {
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        .blendEnable = VK_FALSE,
    };

    VkPipelineColorBlendStateCreateInfo color_blending = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .logicOpEnable = VK_FALSE,
        .attachmentCount = 1,
        .pAttachments = &color_blend_attachment,
    };

    VkPipelineDepthStencilStateCreateInfo depth_stencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = vk_s && vk_s->use_depth ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = vk_s && vk_s->use_depth ? VK_TRUE : VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS,
        .depthBoundsTestEnable = VK_FALSE,
        .stencilTestEnable = VK_FALSE,
    };

    VkPushConstantRange push_range = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = LAPIZ_VK_PUSH_CONSTANT_SIZE,
    };

    VkDescriptorSetLayout set_layouts[1];
    UINT set_layout_count = 0;
    if (vk_s && vk_s->texture_descriptor_layout) 
    {
        set_layouts[0] = vk_s->texture_descriptor_layout;
        set_layout_count = 1;
    }

    VkPipelineLayoutCreateInfo layout_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = set_layout_count,
        .pSetLayouts = set_layout_count ? set_layouts : NULL,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_range,
    };

    VkPipelineLayout layout;

    if (vkCreatePipelineLayout(device, &layout_info, NULL, &layout) != VK_SUCCESS)
        return VK_NULL_HANDLE;

    *out_layout = layout;

    VkGraphicsPipelineCreateInfo pipeline_info = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertex_input,
        .pInputAssemblyState = &input_assembly,
        .pViewportState = &viewport_state,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_stencil,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_state,
        .layout = layout,
        .renderPass = render_pass,
        .subpass = 0,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = -1,
    };

    VkPipeline pipeline;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline_info, NULL, &pipeline) != VK_SUCCESS) 
    {
        vkDestroyPipelineLayout(device, layout, NULL);
        return VK_NULL_HANDLE;
    }
    return pipeline;
}

LapizShader *LapizVKShaderLoadDefault(void) 
{
    g_vk_shader_error[0] = '\0';
    
    if (!vk_s || !vk_s->device || !vk_s->render_pass)
        return NULL;

    VkShaderModule vert_mod = create_shader_module(vk_s->device, vk_default_vert_spv, vk_default_vert_spv_size);
    
    if (!vert_mod) 
    {
        strncpy(g_vk_shader_error, "Failed to create vertex shader module", LAPIZ_SHADER_ERROR_MAX - 1);
        g_vk_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        return NULL;
    }

    VkShaderModule frag_mod = create_shader_module(vk_s->device, vk_default_frag_spv, vk_default_frag_spv_size);
   
    if (!frag_mod) 
    {
        vkDestroyShaderModule(vk_s->device, vert_mod, NULL);
        strncpy(g_vk_shader_error, "Failed to create fragment shader module", LAPIZ_SHADER_ERROR_MAX - 1);
        g_vk_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        return NULL;
    }

    VkPipelineLayout layout;
    VkPipeline pipeline = create_graphics_pipeline(vk_s->device, vk_s->render_pass, vert_mod, frag_mod, vk_s->swapchain_extent, &layout);

    vkDestroyShaderModule(vk_s->device, vert_mod, NULL);
    vkDestroyShaderModule(vk_s->device, frag_mod, NULL);

    if (!pipeline) 
    {
        strncpy(g_vk_shader_error, "Failed to create graphics pipeline", LAPIZ_SHADER_ERROR_MAX - 1);
        g_vk_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        return NULL;
    }

    LapizShader *shader = calloc(1, sizeof(LapizShader));
    
    if (!shader) 
    {
        vkDestroyPipeline(vk_s->device, pipeline, NULL);
        vkDestroyPipelineLayout(vk_s->device, layout, NULL);
        return NULL;
    }

    shader->pipeline = pipeline;
    shader->pipeline_layout = layout;
    shader->color_offset = 0;
    shader->push_constants[0] = 1.0f;
    shader->push_constants[1] = 1.0f;
    shader->push_constants[2] = 1.0f;
    shader->push_constants[3] = 1.0f;

    memset(shader->bound_textures, 0, sizeof(shader->bound_textures));

    return shader;
}

LapizShader *LapizVKShaderLoadFromMemory(const char *vsCode, const char *fsCode) 
{
    (void)vsCode;
    (void)fsCode;
    strncpy(g_vk_shader_error, "Vulkan: use LoadFromFile with .spv files", LAPIZ_SHADER_ERROR_MAX - 1);
    g_vk_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
    return NULL;
}

LapizShader *LapizVKShaderLoadFromFile(const char *vertPath, const char *fragPath) 
{
    g_vk_shader_error[0] = '\0';

    if (!vertPath || !fragPath || !vk_s || !vk_s->device || !vk_s->render_pass)
        return NULL;

    size_t vert_size = 0, frag_size = 0;
    void *vert_data = LapizLoadFileBinary(vertPath, &vert_size);

    if (!vert_data || vert_size == 0) 
    {
        strncpy(g_vk_shader_error, "Failed to load vertex shader file", LAPIZ_SHADER_ERROR_MAX - 1);
        g_vk_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        return NULL;
    }

    void *frag_data = LapizLoadFileBinary(fragPath, &frag_size);

    if (!frag_data || frag_size == 0) 
    {
        free(vert_data);
        strncpy(g_vk_shader_error, "Failed to load fragment shader file", LAPIZ_SHADER_ERROR_MAX - 1);
        g_vk_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        return NULL;
    }

    VkShaderModule vert_mod = create_shader_module(vk_s->device, vert_data, vert_size);
    free(vert_data);

    if (!vert_mod) 
    {
        free(frag_data);
        strncpy(g_vk_shader_error, "Failed to create vertex shader module", LAPIZ_SHADER_ERROR_MAX - 1);
        g_vk_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        return NULL;
    }

    VkShaderModule frag_mod = create_shader_module(vk_s->device, frag_data, frag_size);
    free(frag_data);

    if (!frag_mod) 
    {
        vkDestroyShaderModule(vk_s->device, vert_mod, NULL);
        strncpy(g_vk_shader_error, "Failed to create fragment shader module", LAPIZ_SHADER_ERROR_MAX - 1);
        g_vk_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        return NULL;
    }

    VkPipelineLayout layout;
    VkPipeline pipeline =
        create_graphics_pipeline(vk_s->device, vk_s->render_pass, vert_mod, frag_mod, vk_s->swapchain_extent, &layout);

    vkDestroyShaderModule(vk_s->device, vert_mod, NULL);
    vkDestroyShaderModule(vk_s->device, frag_mod, NULL);

    if (!pipeline) 
    {
        strncpy(g_vk_shader_error, "Failed to create graphics pipeline", LAPIZ_SHADER_ERROR_MAX - 1);
        g_vk_shader_error[LAPIZ_SHADER_ERROR_MAX - 1] = '\0';
        return NULL;
    }

    LapizShader *shader = calloc(1, sizeof(LapizShader));

    if (!shader) 
    {
        vkDestroyPipeline(vk_s->device, pipeline, NULL);
        vkDestroyPipelineLayout(vk_s->device, layout, NULL);
        return NULL;
    }

    shader->pipeline = pipeline;
    shader->pipeline_layout = layout;
    shader->color_offset = 0;
    shader->push_constants[0] = 1.0f;
    shader->push_constants[1] = 1.0f;
    shader->push_constants[2] = 1.0f;
    shader->push_constants[3] = 1.0f;

    memset(shader->bound_textures, 0, sizeof(shader->bound_textures));

    return shader;
}

LapizShader *LapizVKShaderLoadFromFileEx(const char *vertPath, const char *fragPath, const char *vertEntry, const char *fragEntry) 
{
    (void)vertEntry;
    (void)fragEntry;
    return LapizVKShaderLoadFromFile(vertPath, fragPath);
}

void LapizVKShaderUnload(LapizShader *shader) 
{
    if (!shader || !vk_s)
        return;

    if (shader->pipeline) 
    {
        vkDeviceWaitIdle(vk_s->device);
        vkDestroyPipeline(vk_s->device, shader->pipeline, NULL);
    }

    if (shader->pipeline_layout)
        vkDestroyPipelineLayout(vk_s->device, shader->pipeline_layout, NULL);

    shader->pipeline = VK_NULL_HANDLE;
    shader->pipeline_layout = VK_NULL_HANDLE;

    free(shader);
}

int LapizVKShaderIsValid(const LapizShader *shader) 
{
    return (shader && shader->pipeline != VK_NULL_HANDLE) ? 1 : 0;
}

int LapizVKShaderGetLocation(const LapizShader *shader, const char *uniformName) 
{
    if (!shader || !uniformName)
        return -1;
    if (strcmp(uniformName, LAPIZ_SHADER_UNIFORM_COLOR) == 0)
        return 0;
    if (strcmp(uniformName, "iResolution") == 0)
        return 0; /* vec3 at offset 0 */
    if (strcmp(uniformName, "iTime") == 0)
        return 3; /* float at offset 3 (in floats) */
    return -1;
}

int LapizVKShaderGetVertexLocation(const LapizShader *shader, const char *uniformName) 
{
    (void)shader;
    (void)uniformName;
    return -1;
}

int LapizVKShaderGetDefaultLocation(LapizShader *shader, LapizShaderLocationIndex idx) 
{
    if (!shader)
        return -1;

    switch (idx) 
    {
        case LAPIZ_SHADER_LOC_COLOR:
            return 0;
        case LAPIZ_SHADER_LOC_MVP:
            return -1;
        default:
            return -1;
    }
}

void LapizVKShaderUse(LapizShader *shader) 
{
    vk_s->current_shader = shader;
}

void LapizVKShaderSetFloat(LapizShader *shader, int loc, float value) 
{
    if (!shader || loc < 0 || loc >= 4)
        return;

    shader->push_constants[loc] = value;
}

void LapizVKShaderSetVec2(LapizShader *shader, int loc, const float *v) 
{
    if (!shader || loc < 0 || !v)
        return;

    if (loc == 0) 
    {
        shader->push_constants[0] = v[0];
        shader->push_constants[1] = v[1];
    }
}

void LapizVKShaderSetVec3(LapizShader *shader, int loc, const float *v) 
{
    if (!shader || loc < 0 || !v)
        return;

    if (loc == 0) 
    {
        shader->push_constants[0] = v[0];
        shader->push_constants[1] = v[1];
        shader->push_constants[2] = v[2];
    }
}

void LapizVKShaderSetVec4(LapizShader *shader, int loc, const float *v) 
{
    if (!shader || loc < 0 || !v)
        return;

    if (loc == 0) 
    {
        shader->push_constants[0] = v[0];
        shader->push_constants[1] = v[1];
        shader->push_constants[2] = v[2];
        shader->push_constants[3] = v[3];
    }
}

void LapizVKShaderSetInt(LapizShader *shader, int loc, int value) 
{
    (void)shader;
    (void)loc;
    (void)value;
}

void LapizVKShaderSetMatrix4(LapizShader *shader, int loc, const float *m) 
{
    (void)shader;
    (void)loc;
    (void)m;
}

void LapizVKShaderSetColor(LapizShader *shader, int loc, LapizColor color) 
{
    LapizVKShaderSetVec4(shader, loc, color);
}

void LapizVKShaderSetTextureEx(LapizShader *shader, int loc, LapizTexture *texture, int slot) 
{
    (void)loc;

    if (!shader || slot < 0 || slot >= 4)
        return;

    shader->bound_textures[slot] = texture;
}

void LapizVKShaderSetTexture(LapizShader *shader, int loc, LapizTexture *texture) 
{
    LapizVKShaderSetTextureEx(shader, loc, texture, 0);
}

const char *LapizVKShaderGetCompileError(void) 
{
    return (g_vk_shader_error[0] != '\0') ? g_vk_shader_error : NULL;
}

void LapizVKShaderRecordDraw(VkCommandBuffer cmd, LapizShader *shader, VkExtent2D extent) 
{
    if (!cmd || !shader || shader->pipeline == VK_NULL_HANDLE)
        return;

    /* Bind texture descriptor set if pipeline has texture layout */
    if (vk_s && vk_s->texture_descriptor_layout && vk_s->texture_sampler) 
    {
        LapizTexture *tex = (shader->bound_textures[0] && LapizTextureIsValid(shader->bound_textures[0])) ? shader->bound_textures[0] : vk_s->default_texture;
        VkImageView view = tex ? LapizVKTextureGetImageView(tex) : VK_NULL_HANDLE;
        
        if (view) 
        {
            VkDescriptorImageInfo img_info = {
                .sampler = vk_s->texture_sampler,
                .imageView = view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            VkWriteDescriptorSet write = {
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = vk_s->texture_descriptor_sets[vk_s->current_frame],
                .dstBinding = 0,
                .dstArrayElement = 0,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                .pImageInfo = &img_info,
            };
            vkUpdateDescriptorSets(vk_s->device, 1, &write, 0, NULL);
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline_layout, 0, 1, &vk_s->texture_descriptor_sets[vk_s->current_frame], 0, NULL);
        }
    }

    VkViewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = (float)extent.width,
        .height = (float)extent.height,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };
    VkRect2D scissor = {{0, 0}, extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, shader->pipeline);
    vkCmdPushConstants(cmd, shader->pipeline_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(LapizColor),
                       shader->push_constants);
    vkCmdDraw(cmd, 3, 1, 0, 0);
}
