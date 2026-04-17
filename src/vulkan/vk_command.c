#include "vulkan_internal.h"
#include <stdlib.h>
#include <string.h>

// COMMAND BUFFER IMPLEMENTATION
// ============================================================================

LPZ_INLINE VkShaderStageFlags lpz_vk_shader_stage(LpzShaderStage s)
{
    VkShaderStageFlags flags = 0;
    if (s & LPZ_SHADER_STAGE_VERTEX)
        flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (s & LPZ_SHADER_STAGE_FRAGMENT)
        flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (s & LPZ_SHADER_STAGE_COMPUTE)
        flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (s & LPZ_SHADER_STAGE_MESH)
        flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
    return flags ? flags : VK_SHADER_STAGE_ALL_GRAPHICS;
}

// ----------------------------------------------------------------------------
// Begin / End
// ----------------------------------------------------------------------------

static lpz_command_buffer_t vk_cmd_begin(lpz_device_t device_handle)
{
    struct device_t *dev = vk_dev(device_handle);
    if (!dev)
        return LPZ_COMMAND_BUFFER_NULL;

    lpz_handle_t h = lpz_pool_alloc(&g_vk_cmd_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_COMMAND_BUFFER_NULL;

    lpz_command_buffer_t cmd_handle = {h};
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    memset(cb, 0, sizeof(*cb));
    cb->device = device_handle;
    cb->frameIndex = dev->frameIndex;
    cb->ended = false;
    cb->activePipeline = (lpz_pipeline_t){LPZ_HANDLE_NULL};
    cb->activeComputePipeline = (lpz_compute_pipeline_t){LPZ_HANDLE_NULL};
    cb->activeDepthStencilState = (lpz_depth_stencil_state_t){LPZ_HANDLE_NULL};

    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = dev->graphicsQueueFamily,
    };
    if (vkCreateCommandPool(dev->device, &pci, NULL, &cb->pool) != VK_SUCCESS)
    {
        lpz_pool_free(&g_vk_cmd_pool, h);
        return LPZ_COMMAND_BUFFER_NULL;
    }

    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = cb->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(dev->device, &ai, &cb->cmd) != VK_SUCCESS)
    {
        vkDestroyCommandPool(dev->device, cb->pool, NULL);
        lpz_pool_free(&g_vk_cmd_pool, h);
        return LPZ_COMMAND_BUFFER_NULL;
    }

    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(cb->cmd, &bi);
    return cmd_handle;
}

/* End — vkEndCommandBuffer is deferred to Submit so the renderer can inject
 * present-src barriers on the last command buffer before closing it. */
static void vk_cmd_end(lpz_command_buffer_t cmd_handle)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    cb->ended = true;
}

// ----------------------------------------------------------------------------
// Render pass
// ----------------------------------------------------------------------------

static void vk_cmd_begin_render_pass(lpz_command_buffer_t cmd_handle, const LpzRenderPassDesc *desc)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !desc)
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    VkCommandBuffer cmd = cb->cmd;

    // Transition color attachments to COLOR_ATTACHMENT_OPTIMAL.
    for (uint32_t i = 0; i < desc->color_attachment_count; i++)
    {
        if (LPZ_HANDLE_VALID(desc->color_attachments[i].texture))
            lpz_vk_transition_tracked(cmd, vk_tex(desc->color_attachments[i].texture), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // Transition depth attachment.
    if (desc->depth_attachment && LPZ_HANDLE_VALID(desc->depth_attachment->texture))
        lpz_vk_transition_tracked(cmd, vk_tex(desc->depth_attachment->texture), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL, VK_IMAGE_ASPECT_DEPTH_BIT);

    // Derive render area from first color or depth attachment.
    uint32_t width = 0, height = 0;
    if (desc->color_attachment_count > 0 && LPZ_HANDLE_VALID(desc->color_attachments[0].texture))
    {
        struct texture_t *t = vk_tex(desc->color_attachments[0].texture);
        width = t->width;
        height = t->height;
    }
    else if (desc->depth_attachment && LPZ_HANDLE_VALID(desc->depth_attachment->texture))
    {
        struct texture_t *t = vk_tex(desc->depth_attachment->texture);
        width = t->width;
        height = t->height;
    }

    if (g_has_dynamic_render && g_vkCmdBeginRendering)
    {
        VkRenderingAttachmentInfoKHR color_infos[8];
        for (uint32_t i = 0; i < desc->color_attachment_count && i < 8; i++)
        {
            const LpzColorAttachment *a = &desc->color_attachments[i];
            VkImageView iv = VK_NULL_HANDLE;
            if (LPZ_HANDLE_VALID(a->texture_view))
                iv = vk_tex_view(a->texture_view)->imageView;
            else if (LPZ_HANDLE_VALID(a->texture))
                iv = vk_tex(a->texture)->imageView;

            color_infos[i] = (VkRenderingAttachmentInfoKHR){
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
                .imageView = iv,
                .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .loadOp = LpzToVkLoadOp(a->load_op),
                .storeOp = LpzToVkStoreOp(a->store_op),
                .clearValue.color = {{a->clear_color.r, a->clear_color.g, a->clear_color.b, a->clear_color.a}},
            };

            if (LPZ_HANDLE_VALID(a->resolve_texture))
            {
                VkImageView riv = LPZ_HANDLE_VALID(a->resolve_texture_view) ? vk_tex_view(a->resolve_texture_view)->imageView : vk_tex(a->resolve_texture)->imageView;
                color_infos[i].resolveImageView = riv;
                color_infos[i].resolveImageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
                color_infos[i].resolveMode = VK_RESOLVE_MODE_AVERAGE_BIT;
            }
        }

        VkRenderingAttachmentInfoKHR depth_info = {0};
        bool has_depth = desc->depth_attachment && LPZ_HANDLE_VALID(desc->depth_attachment->texture);
        if (has_depth)
        {
            const LpzDepthAttachment *d = desc->depth_attachment;
            VkImageView div = LPZ_HANDLE_VALID(d->texture_view) ? vk_tex_view(d->texture_view)->imageView : vk_tex(d->texture)->imageView;

            depth_info = (VkRenderingAttachmentInfoKHR){
                .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR,
                .imageView = div,
                .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .loadOp = LpzToVkLoadOp(d->load_op),
                .storeOp = LpzToVkStoreOp(d->store_op),
                .clearValue.depthStencil =
                    {
                        .depth = d->clear_depth,
                        .stencil = d->clear_stencil,
                    },
            };
        }

        VkRenderingInfoKHR ri = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO_KHR,
            .renderArea = {{0, 0}, {width, height}},
            .layerCount = 1,
            .colorAttachmentCount = desc->color_attachment_count,
            .pColorAttachments = color_infos,
            .pDepthAttachment = has_depth ? &depth_info : NULL,
        };
        g_vkCmdBeginRendering(cmd, &ri);
    }
    else
    {
        LPZ_WARN("[Vulkan] BeginRenderPass: dynamic rendering unavailable; pass skipped.");
    }

    // Set default full-framebuffer viewport and scissor if not already configured.
    if (!cb->viewportValid && width && height)
    {
        // Y-flip: Lapiz convention is Y-up NDC; Vulkan is Y-down.
        VkViewport vp = {
            .x = 0.0f,
            .y = (float)height,
            .width = (float)width,
            .height = -(float)height,
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(cmd, 0, 1, &vp);
        cb->cachedViewport = vp;
        cb->viewportValid = true;
    }
    if (!cb->scissorValid && width && height)
    {
        VkRect2D sc = {{0, 0}, {width, height}};
        vkCmdSetScissor(cmd, 0, 1, &sc);
        cb->cachedScissor = sc;
        cb->scissorValid = true;
    }
}

static void vk_cmd_end_render_pass(lpz_command_buffer_t cmd_handle)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    if (g_has_dynamic_render && g_vkCmdEndRendering)
        g_vkCmdEndRendering(cb->cmd);
    cb->viewportValid = false;
    cb->scissorValid = false;
}

// ----------------------------------------------------------------------------
// Viewport / scissor / dynamic state
// ----------------------------------------------------------------------------

static void vk_cmd_set_viewport(lpz_command_buffer_t cmd_handle, float x, float y, float width, float height, float min_depth, float max_depth)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    // Y-flip for Lapiz Y-up convention.
    VkViewport vp = {
        .x = x,
        .y = y + height,
        .width = width,
        .height = -height,
        .minDepth = min_depth,
        .maxDepth = max_depth,
    };
    vkCmdSetViewport(cb->cmd, 0, 1, &vp);
    cb->cachedViewport = vp;
    cb->viewportValid = true;
}

/* xywh_min_max layout: [x, y, width, height, min_depth, max_depth] per viewport */
static void vk_cmd_set_viewports(lpz_command_buffer_t cmd_handle, uint32_t first, uint32_t count, const float *xywh_min_max)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !xywh_min_max || !count)
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    VkViewport stack[8];
    VkViewport *vps = (count <= 8) ? stack : malloc(sizeof(VkViewport) * count);
    for (uint32_t i = 0; i < count; i++)
    {
        const float *v = xywh_min_max + i * 6;
        vps[i] = (VkViewport){
            .x = v[0],
            .y = v[1] + v[3],
            .width = v[2],
            .height = -v[3],
            .minDepth = v[4],
            .maxDepth = v[5],
        };
    }
    vkCmdSetViewport(cb->cmd, first, count, vps);
    if (vps != stack)
        free(vps);
}

static void vk_cmd_set_scissor(lpz_command_buffer_t cmd_handle, uint32_t x, uint32_t y, uint32_t width, uint32_t height)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    VkRect2D sc = {{(int32_t)x, (int32_t)y}, {width, height}};
    vkCmdSetScissor(cb->cmd, 0, 1, &sc);
    cb->cachedScissor = sc;
    cb->scissorValid = true;
}

/* xywh layout: [x, y, width, height] per scissor rect */
static void vk_cmd_set_scissors(lpz_command_buffer_t cmd_handle, uint32_t first, uint32_t count, const uint32_t *xywh)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !xywh || !count)
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    VkRect2D stack[8];
    VkRect2D *scs = (count <= 8) ? stack : malloc(sizeof(VkRect2D) * count);
    for (uint32_t i = 0; i < count; i++)
    {
        const uint32_t *r = xywh + i * 4;
        scs[i] = (VkRect2D){{(int32_t)r[0], (int32_t)r[1]}, {r[2], r[3]}};
    }
    vkCmdSetScissor(cb->cmd, first, count, scs);
    if (scs != stack)
        free(scs);
}

static void vk_cmd_set_depth_bias(lpz_command_buffer_t cmd_handle, float constant, float slope, float clamp)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    vkCmdSetDepthBias(vk_cmd(cmd_handle)->cmd, constant, clamp, slope);
}

static void vk_cmd_set_stencil_reference(lpz_command_buffer_t cmd_handle, uint32_t reference)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    vkCmdSetStencilReference(vk_cmd(cmd_handle)->cmd, VK_STENCIL_FACE_FRONT_AND_BACK, reference);
}

// ----------------------------------------------------------------------------
// Pipeline / depth-stencil binding
// ----------------------------------------------------------------------------

static void vk_cmd_bind_pipeline(lpz_command_buffer_t cmd_handle, lpz_pipeline_t pipeline_handle)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(pipeline_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    struct pipeline_t *p = vk_pipe(pipeline_handle);
    vkCmdBindPipeline(cb->cmd, p->bindPoint, p->pipeline);
    cb->activePipeline = pipeline_handle;
}

static void vk_cmd_bind_depth_stencil_state(lpz_command_buffer_t cmd_handle, lpz_depth_stencil_state_t dss_handle)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    cb->activeDepthStencilState = dss_handle;

    if (!LPZ_HANDLE_VALID(dss_handle) || !g_has_ext_dyn_state)
        return;
    struct depth_stencil_state_t *d = vk_dss(dss_handle);
    VkCommandBuffer cmd = cb->cmd;

    if (g_vkCmdSetDepthTestEnable)
        g_vkCmdSetDepthTestEnable(cmd, d->depth_test_enable ? VK_TRUE : VK_FALSE);
    if (g_vkCmdSetDepthWriteEnable)
        g_vkCmdSetDepthWriteEnable(cmd, d->depth_write_enable ? VK_TRUE : VK_FALSE);
    if (g_vkCmdSetDepthCompareOp)
        g_vkCmdSetDepthCompareOp(cmd, d->depth_compare_op);
    if (g_vkCmdSetStencilTestEnable)
        g_vkCmdSetStencilTestEnable(cmd, d->stencil_test_enable ? VK_TRUE : VK_FALSE);
    if (d->stencil_test_enable)
    {
        if (g_vkCmdSetStencilOp)
        {
            g_vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_BIT, d->front.failOp, d->front.passOp, d->front.depthFailOp, d->front.compareOp);
            g_vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_BACK_BIT, d->back.failOp, d->back.passOp, d->back.depthFailOp, d->back.compareOp);
        }
        if (g_vkCmdSetStencilCompareMask)
            g_vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d->stencil_read_mask);
        if (g_vkCmdSetStencilWriteMask)
            g_vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, d->stencil_write_mask);
    }
}

// ----------------------------------------------------------------------------
// Vertex / index buffer binding
// ----------------------------------------------------------------------------

static void vk_cmd_bind_vertex_buffers(lpz_command_buffer_t cmd_handle, uint32_t first_binding, uint32_t count, const lpz_buffer_t *buffers, const uint64_t *offsets)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !buffers || !count)
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    uint32_t fi = cb->frameIndex;

    VkBuffer stack_bufs[8];
    VkBuffer *vk_bufs = (count <= 8) ? stack_bufs : malloc(sizeof(VkBuffer) * count);
    for (uint32_t i = 0; i < count; i++)
        vk_bufs[i] = LPZ_HANDLE_VALID(buffers[i]) ? vk_buf_get(buffers[i], fi) : VK_NULL_HANDLE;

    vkCmdBindVertexBuffers(cb->cmd, first_binding, count, vk_bufs, offsets ? offsets : (const uint64_t[]){0});
    if (vk_bufs != stack_bufs)
        free(vk_bufs);
}

static void vk_cmd_bind_index_buffer(lpz_command_buffer_t cmd_handle, lpz_buffer_t buffer_handle, uint64_t offset, LpzIndexType type)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(buffer_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    VkIndexType vk_type = (type == LPZ_INDEX_TYPE_UINT32) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(cb->cmd, vk_buf_get(buffer_handle, cb->frameIndex), offset, vk_type);
}

// ----------------------------------------------------------------------------
// Bind groups / push constants
// ----------------------------------------------------------------------------

static void vk_cmd_bind_bind_group(lpz_command_buffer_t cmd_handle, uint32_t set, lpz_bind_group_t bg_handle, const uint32_t *dynamic_offsets, uint32_t dynamic_offset_count)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(bg_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);

    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipelineBindPoint bind_point = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if (LPZ_HANDLE_VALID(cb->activePipeline))
    {
        struct pipeline_t *p = vk_pipe(cb->activePipeline);
        layout = p->pipelineLayout;
        bind_point = p->bindPoint;
    }
    else if (LPZ_HANDLE_VALID(cb->activeComputePipeline))
    {
        layout = vk_cpipe(cb->activeComputePipeline)->pipelineLayout;
        bind_point = VK_PIPELINE_BIND_POINT_COMPUTE;
    }
    if (layout == VK_NULL_HANDLE)
        return;

    struct bind_group_t *bg = vk_bg(bg_handle);
    vkCmdBindDescriptorSets(cb->cmd, bind_point, layout, set, 1, &bg->set, dynamic_offset_count, dynamic_offsets);
}

static void vk_cmd_push_constants(lpz_command_buffer_t cmd_handle, LpzShaderStage stage, uint32_t offset, uint32_t size, const void *data)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !data || !size)
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);

    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (LPZ_HANDLE_VALID(cb->activePipeline))
        layout = vk_pipe(cb->activePipeline)->pipelineLayout;
    else if (LPZ_HANDLE_VALID(cb->activeComputePipeline))
        layout = vk_cpipe(cb->activeComputePipeline)->pipelineLayout;
    if (layout == VK_NULL_HANDLE)
        return;

    // The pipeline layout declares its push constant range with VK_SHADER_STAGE_ALL_GRAPHICS
    // (graphics) or VK_SHADER_STAGE_ALL (compute). The stage flags passed to
    // vkCmdPushConstants must be a subset of those declared in the layout, so we
    // use the layout's own flags rather than converting the caller's LpzShaderStage.
    VkShaderStageFlags vk_stage = LPZ_HANDLE_VALID(cb->activeComputePipeline) ? VK_SHADER_STAGE_ALL : VK_SHADER_STAGE_ALL_GRAPHICS;
    vkCmdPushConstants(cb->cmd, layout, vk_stage, offset, size, data);
}

// ----------------------------------------------------------------------------
// Draw calls
// ----------------------------------------------------------------------------

static void vk_cmd_draw(lpz_command_buffer_t cmd_handle, uint32_t vertex_count, uint32_t instance_count, uint32_t first_vertex, uint32_t first_instance)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    vkCmdDraw(vk_cmd(cmd_handle)->cmd, vertex_count, instance_count, first_vertex, first_instance);
}

static void vk_cmd_draw_indexed(lpz_command_buffer_t cmd_handle, uint32_t index_count, uint32_t instance_count, uint32_t first_index, int32_t vertex_offset, uint32_t first_instance)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    vkCmdDrawIndexed(vk_cmd(cmd_handle)->cmd, index_count, instance_count, first_index, vertex_offset, first_instance);
}

static void vk_cmd_draw_indirect(lpz_command_buffer_t cmd_handle, lpz_buffer_t buffer_handle, uint64_t offset, uint32_t draw_count)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(buffer_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    vkCmdDrawIndirect(cb->cmd, vk_buf_get(buffer_handle, cb->frameIndex), offset, draw_count, sizeof(VkDrawIndirectCommand));
}

static void vk_cmd_draw_indexed_indirect(lpz_command_buffer_t cmd_handle, lpz_buffer_t buffer_handle, uint64_t offset, uint32_t draw_count)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(buffer_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    vkCmdDrawIndexedIndirect(cb->cmd, vk_buf_get(buffer_handle, cb->frameIndex), offset, draw_count, sizeof(VkDrawIndexedIndirectCommand));
}

static void vk_cmd_draw_indirect_count(lpz_command_buffer_t cmd_handle, lpz_buffer_t buffer_handle, uint64_t offset, lpz_buffer_t count_buffer_handle, uint64_t count_offset, uint32_t max_draw_count)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(buffer_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    uint32_t fi = cb->frameIndex;
    if (g_has_draw_indirect_count && g_vkCmdDrawIndirectCount)
        g_vkCmdDrawIndirectCount(cb->cmd, vk_buf_get(buffer_handle, fi), offset, vk_buf_get(count_buffer_handle, fi), count_offset, max_draw_count, sizeof(VkDrawIndirectCommand));
    else
        LPZ_WARN("[Vulkan] DrawIndirectCount not supported; call ignored.");
}

static void vk_cmd_draw_indexed_indirect_count(lpz_command_buffer_t cmd_handle, lpz_buffer_t buffer_handle, uint64_t offset, lpz_buffer_t count_buffer_handle, uint64_t count_offset, uint32_t max_draw_count)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(buffer_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    uint32_t fi = cb->frameIndex;
    if (g_has_draw_indirect_count && g_vkCmdDrawIndexedIndirectCount)
        g_vkCmdDrawIndexedIndirectCount(cb->cmd, vk_buf_get(buffer_handle, fi), offset, vk_buf_get(count_buffer_handle, fi), count_offset, max_draw_count, sizeof(VkDrawIndexedIndirectCommand));
    else
        LPZ_WARN("[Vulkan] DrawIndexedIndirectCount not supported; call ignored.");
}

// ----------------------------------------------------------------------------
// Compute pass
// ----------------------------------------------------------------------------

/* Vulkan has no separate compute pass object — BeginComputePass / EndComputePass
 * are no-ops; compute commands are recorded directly into the command buffer. */
static void vk_cmd_begin_compute_pass(lpz_command_buffer_t cmd_handle)
{
    (void)cmd_handle;
}

static void vk_cmd_end_compute_pass(lpz_command_buffer_t cmd_handle)
{
    (void)cmd_handle;
}

static void vk_cmd_bind_compute_pipeline(lpz_command_buffer_t cmd_handle, lpz_compute_pipeline_t pipeline_handle)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(pipeline_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    struct compute_pipeline_t *p = vk_cpipe(pipeline_handle);
    vkCmdBindPipeline(cb->cmd, VK_PIPELINE_BIND_POINT_COMPUTE, p->pipeline);
    cb->activeComputePipeline = pipeline_handle;
}

static void vk_cmd_dispatch_compute(lpz_command_buffer_t cmd_handle, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    vkCmdDispatch(vk_cmd(cmd_handle)->cmd, group_count_x, group_count_y, group_count_z);
}

static void vk_cmd_dispatch_compute_indirect(lpz_command_buffer_t cmd_handle, lpz_buffer_t buffer_handle, uint64_t offset)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(buffer_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    vkCmdDispatchIndirect(cb->cmd, vk_buf_get(buffer_handle, cb->frameIndex), offset);
}

// ----------------------------------------------------------------------------
// Barriers
// ----------------------------------------------------------------------------

static void vk_cmd_pipeline_barrier(lpz_command_buffer_t cmd_handle, const LpzBarrierDesc *desc)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !desc)
        return;
    VkCommandBuffer cmd = vk_cmd(cmd_handle)->cmd;

    for (uint32_t i = 0; i < desc->texture_barrier_count; i++)
    {
        const LpzTextureBarrier *tb = &desc->texture_barriers[i];
        if (!LPZ_HANDLE_VALID(tb->texture))
            continue;
        struct texture_t *tex = vk_tex(tb->texture);

        // Determine aspect from format.
        VkImageAspectFlags aspect = is_depth_format(tex->format) ? (VK_IMAGE_ASPECT_DEPTH_BIT | (is_stencil_format(tex->format) ? VK_IMAGE_ASPECT_STENCIL_BIT : 0)) : VK_IMAGE_ASPECT_COLOR_BIT;

        lpz_vk_transition_tracked(cmd, tex, tex->currentLayout, aspect);
    }

    for (uint32_t i = 0; i < desc->buffer_barrier_count; i++)
    {
        const LpzBufferBarrier *bb = &desc->buffer_barriers[i];
        if (!LPZ_HANDLE_VALID(bb->buffer))
            continue;
        lpz_vk_memory_barrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    }
}

// ----------------------------------------------------------------------------
// Queries
// ----------------------------------------------------------------------------

static void vk_cmd_reset_query_pool(lpz_command_buffer_t cmd_handle, lpz_query_pool_t pool_handle, uint32_t first, uint32_t count)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(pool_handle))
        return;
    struct query_pool_t *qp = vk_qpool(pool_handle);
    vkCmdResetQueryPool(vk_cmd(cmd_handle)->cmd, qp->pool, first, count);
}

static void vk_cmd_write_timestamp(lpz_command_buffer_t cmd_handle, lpz_query_pool_t pool_handle, uint32_t index)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(pool_handle))
        return;
    struct query_pool_t *qp = vk_qpool(pool_handle);
    vkCmdWriteTimestamp(vk_cmd(cmd_handle)->cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp->pool, index);
}

static void vk_cmd_begin_query(lpz_command_buffer_t cmd_handle, lpz_query_pool_t pool_handle, uint32_t index)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(pool_handle))
        return;
    struct query_pool_t *qp = vk_qpool(pool_handle);
    VkQueryControlFlags flags = (qp->type == LPZ_QUERY_TYPE_OCCLUSION) ? VK_QUERY_CONTROL_PRECISE_BIT : 0;
    vkCmdBeginQuery(vk_cmd(cmd_handle)->cmd, qp->pool, index, flags);
}

static void vk_cmd_end_query(lpz_command_buffer_t cmd_handle, lpz_query_pool_t pool_handle, uint32_t index)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(pool_handle))
        return;
    vkCmdEndQuery(vk_cmd(cmd_handle)->cmd, vk_qpool(pool_handle)->pool, index);
}

// ----------------------------------------------------------------------------
// Debug labels
// ----------------------------------------------------------------------------

static void vk_cmd_begin_debug_label(lpz_command_buffer_t cmd_handle, const char *label, float r, float g, float b)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    struct device_t *dev = vk_dev(cb->device);
    if (!dev->pfnCmdBeginDebugLabel || !label)
        return;
    VkDebugUtilsLabelEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = label,
        .color = {r, g, b, 1.0f},
    };
    dev->pfnCmdBeginDebugLabel(cb->cmd, &info);
}

static void vk_cmd_end_debug_label(lpz_command_buffer_t cmd_handle)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    struct device_t *dev = vk_dev(cb->device);
    if (dev->pfnCmdEndDebugLabel)
        dev->pfnCmdEndDebugLabel(cb->cmd);
}

static void vk_cmd_insert_debug_label(lpz_command_buffer_t cmd_handle, const char *label, float r, float g, float b)
{
    if (!LPZ_HANDLE_VALID(cmd_handle))
        return;
    struct command_buffer_t *cb = vk_cmd(cmd_handle);
    struct device_t *dev = vk_dev(cb->device);
    if (!dev->pfnCmdInsertDebugLabel || !label)
        return;
    VkDebugUtilsLabelEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = label,
        .color = {r, g, b, 1.0f},
    };
    dev->pfnCmdInsertDebugLabel(cb->cmd, &info);
}

// ----------------------------------------------------------------------------
// Render bundles
// ----------------------------------------------------------------------------

static lpz_render_bundle_t vk_cmd_record_render_bundle(lpz_device_t device_handle, void (*record_fn)(lpz_command_buffer_t, void *), void *userdata)
{
    if (!record_fn)
        return LPZ_RENDER_BUNDLE_NULL;
    struct device_t *dev = vk_dev(device_handle);
    if (!dev)
        return LPZ_RENDER_BUNDLE_NULL;

    lpz_handle_t h = lpz_pool_alloc(&g_vk_bundle_pool);
    if (h == LPZ_HANDLE_NULL)
        return LPZ_RENDER_BUNDLE_NULL;

    lpz_render_bundle_t bundle_handle = {h};
    struct render_bundle_t *rb = vk_bundle(bundle_handle);
    rb->device = device_handle;

    // Create a dedicated command pool for the bundle.
    VkCommandPoolCreateInfo pci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = dev->graphicsQueueFamily,
    };
    if (vkCreateCommandPool(dev->device, &pci, NULL, &rb->pool) != VK_SUCCESS)
    {
        lpz_pool_free(&g_vk_bundle_pool, h);
        return LPZ_RENDER_BUNDLE_NULL;
    }

    VkCommandBufferAllocateInfo ai = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = rb->pool,
        .level = VK_COMMAND_BUFFER_LEVEL_SECONDARY,
        .commandBufferCount = 1,
    };
    if (vkAllocateCommandBuffers(dev->device, &ai, &rb->cmd) != VK_SUCCESS)
    {
        vkDestroyCommandPool(dev->device, rb->pool, NULL);
        lpz_pool_free(&g_vk_bundle_pool, h);
        return LPZ_RENDER_BUNDLE_NULL;
    }

    VkCommandBufferInheritanceInfo inh = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
    };
    VkCommandBufferBeginInfo bi = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT | VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT,
        .pInheritanceInfo = &inh,
    };
    vkBeginCommandBuffer(rb->cmd, &bi);

    // Record into a temporary primary-like handle backed by the secondary buffer.
    lpz_handle_t tmp_h = lpz_pool_alloc(&g_vk_cmd_pool);
    if (tmp_h != LPZ_HANDLE_NULL)
    {
        lpz_command_buffer_t tmp = {tmp_h};
        struct command_buffer_t *cb = vk_cmd(tmp);
        memset(cb, 0, sizeof(*cb));
        cb->cmd = rb->cmd;
        cb->pool = rb->pool;
        cb->device = device_handle;
        cb->activePipeline = (lpz_pipeline_t){LPZ_HANDLE_NULL};
        cb->activeComputePipeline = (lpz_compute_pipeline_t){LPZ_HANDLE_NULL};
        cb->activeDepthStencilState = (lpz_depth_stencil_state_t){LPZ_HANDLE_NULL};

        record_fn(tmp, userdata);
        lpz_pool_free(&g_vk_cmd_pool, tmp_h);
    }

    vkEndCommandBuffer(rb->cmd);
    return bundle_handle;
}

static void vk_cmd_destroy_render_bundle(lpz_render_bundle_t bundle_handle)
{
    if (!LPZ_HANDLE_VALID(bundle_handle))
        return;
    struct render_bundle_t *rb = vk_bundle(bundle_handle);
    struct device_t *dev = vk_dev(rb->device);
    vkFreeCommandBuffers(dev->device, rb->pool, 1, &rb->cmd);
    vkDestroyCommandPool(dev->device, rb->pool, NULL);
    lpz_pool_free(&g_vk_bundle_pool, bundle_handle.h);
}

static void vk_cmd_execute_render_bundle(lpz_command_buffer_t cmd_handle, lpz_render_bundle_t bundle_handle)
{
    if (!LPZ_HANDLE_VALID(cmd_handle) || !LPZ_HANDLE_VALID(bundle_handle))
        return;
    struct render_bundle_t *rb = vk_bundle(bundle_handle);
    vkCmdExecuteCommands(vk_cmd(cmd_handle)->cmd, 1, &rb->cmd);
}

// ----------------------------------------------------------------------------
// Bindless pool (stub — bindless descriptor management is device-side)
// ----------------------------------------------------------------------------

static void vk_cmd_bind_bindless_pool(lpz_command_buffer_t cmd_handle, lpz_bindless_pool_t pool_handle)
{
    /* Bindless pools are bound via descriptor sets managed in vk_device.c.
     * Nothing extra is needed at command-record time for the base Vulkan path. */
    (void)cmd_handle;
    (void)pool_handle;
}

// ============================================================================
// API TABLE
// ============================================================================

const LpzCommandAPI LpzVulkanCommand = {
    .api_version = LPZ_COMMAND_API_VERSION,

    .Begin = vk_cmd_begin,
    .End = vk_cmd_end,

    .BeginRenderPass = vk_cmd_begin_render_pass,
    .EndRenderPass = vk_cmd_end_render_pass,

    .SetViewport = vk_cmd_set_viewport,
    .SetViewports = vk_cmd_set_viewports,
    .SetScissor = vk_cmd_set_scissor,
    .SetScissors = vk_cmd_set_scissors,
    .SetDepthBias = vk_cmd_set_depth_bias,
    .SetStencilReference = vk_cmd_set_stencil_reference,

    .BindPipeline = vk_cmd_bind_pipeline,
    .BindDepthStencilState = vk_cmd_bind_depth_stencil_state,

    .BindVertexBuffers = vk_cmd_bind_vertex_buffers,
    .BindIndexBuffer = vk_cmd_bind_index_buffer,

    .BindBindGroup = vk_cmd_bind_bind_group,
    .PushConstants = vk_cmd_push_constants,

    .Draw = vk_cmd_draw,
    .DrawIndexed = vk_cmd_draw_indexed,
    .DrawIndirect = vk_cmd_draw_indirect,
    .DrawIndexedIndirect = vk_cmd_draw_indexed_indirect,
    .DrawIndirectCount = vk_cmd_draw_indirect_count,
    .DrawIndexedIndirectCount = vk_cmd_draw_indexed_indirect_count,

    .BeginComputePass = vk_cmd_begin_compute_pass,
    .EndComputePass = vk_cmd_end_compute_pass,
    .BindComputePipeline = vk_cmd_bind_compute_pipeline,
    .DispatchCompute = vk_cmd_dispatch_compute,
    .DispatchComputeIndirect = vk_cmd_dispatch_compute_indirect,

    .PipelineBarrier = vk_cmd_pipeline_barrier,

    .ResetQueryPool = vk_cmd_reset_query_pool,
    .WriteTimestamp = vk_cmd_write_timestamp,
    .BeginQuery = vk_cmd_begin_query,
    .EndQuery = vk_cmd_end_query,

    .BeginDebugLabel = vk_cmd_begin_debug_label,
    .EndDebugLabel = vk_cmd_end_debug_label,
    .InsertDebugLabel = vk_cmd_insert_debug_label,

    .RecordRenderBundle = vk_cmd_record_render_bundle,
    .DestroyRenderBundle = vk_cmd_destroy_render_bundle,
    .ExecuteRenderBundle = vk_cmd_execute_render_bundle,

    .BindBindlessPool = vk_cmd_bind_bindless_pool,
};
