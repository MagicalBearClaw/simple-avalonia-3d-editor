#include "OffscreenTarget.h"
#include "VulkanContext.h"

#include <array>
#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void createImage(VmaAllocator    allocator,
                        uint32_t        w,
                        uint32_t        h,
                        VkFormat        format,
                        VkImageUsageFlags usage,
                        VkImage&        outImage,
                        VmaAllocation&  outAlloc)
{
    VkImageCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType     = VK_IMAGE_TYPE_2D;
    ci.format        = format;
    ci.extent        = {w, h, 1};
    ci.mipLevels     = 1;
    ci.arrayLayers   = 1;
    ci.samples       = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
    ci.usage         = usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo ai{};
    ai.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(allocator, &ci, &ai, &outImage, &outAlloc, nullptr) != VK_SUCCESS)
        throw std::runtime_error("Failed to create image");
}

// ---------------------------------------------------------------------------
// SelectDepthFormat
// ---------------------------------------------------------------------------
VkFormat OffscreenTarget::SelectDepthFormat(VkPhysicalDevice pd)
{
    for (VkFormat fmt : {VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D32_SFLOAT_S8_UINT}) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(pd, fmt, &props);
        if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
            return fmt;
    }
    throw std::runtime_error("No suitable depth+stencil format found");
}

// ---------------------------------------------------------------------------
// OffscreenTarget ctor / dtor / Recreate
// ---------------------------------------------------------------------------
OffscreenTarget::OffscreenTarget(VulkanContext& ctx, uint32_t w, uint32_t h)
{
    m_allocator = ctx.allocator;
    depthFormat = SelectDepthFormat(ctx.physicalDevice);
    CreateRenderPass(ctx);
    CreateTransientResources(ctx, w, h);
}

OffscreenTarget::~OffscreenTarget()
{
    DestroyTransientResources();
    if (renderPass != VK_NULL_HANDLE) {
        VmaAllocatorInfo info{};
        vmaGetAllocatorInfo(m_allocator, &info);
        vkDestroyRenderPass(info.device, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }
}

void OffscreenTarget::Recreate(VulkanContext& ctx, uint32_t w, uint32_t h)
{
    vkDeviceWaitIdle(ctx.device);
    DestroyTransientResources();
    CreateTransientResources(ctx, w, h);
}

// ---------------------------------------------------------------------------
// CreateRenderPass — called ONCE at construction; never on resize
// ---------------------------------------------------------------------------
void OffscreenTarget::CreateRenderPass(VulkanContext& ctx)
{
    std::array<VkAttachmentDescription, 2> attachments{};

    // attachment 0: color
    attachments[0].format         = kColorFormat;
    attachments[0].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // attachment 1: depth+stencil
    attachments[1].format         = depthFormat;
    attachments[1].samples        = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount    = 1;
    subpass.pColorAttachments       = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.srcAccessMask = 0;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo ci{};
    ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = static_cast<uint32_t>(attachments.size());
    ci.pAttachments    = attachments.data();
    ci.subpassCount    = 1;
    ci.pSubpasses      = &subpass;
    ci.dependencyCount = 1;
    ci.pDependencies   = &dep;

    if (vkCreateRenderPass(ctx.device, &ci, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("Failed to create render pass");
}

// ---------------------------------------------------------------------------
// CreateTransientResources / DestroyTransientResources
// ---------------------------------------------------------------------------
void OffscreenTarget::CreateTransientResources(VulkanContext& ctx, uint32_t w, uint32_t h)
{
    width  = w;
    height = h;

    // ----- Color image -----
    createImage(m_allocator, w, h, kColorFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                image, imageAlloc);

    // ----- Color image view -----
    {
        VkImageViewCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image            = image;
        ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        ci.format           = kColorFormat;
        ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        if (vkCreateImageView(ctx.device, &ci, nullptr, &imageView) != VK_SUCCESS)
            throw std::runtime_error("Failed to create color image view");
    }

    // ----- Depth+stencil image -----
    createImage(m_allocator, w, h, depthFormat,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                depthImage, depthAlloc);

    // ----- Depth+stencil image view -----
    {
        VkImageViewCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image            = depthImage;
        ci.viewType         = VK_IMAGE_VIEW_TYPE_2D;
        ci.format           = depthFormat;
        ci.subresourceRange = {
            VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT,
            0, 1, 0, 1};

        if (vkCreateImageView(ctx.device, &ci, nullptr, &depthImageView) != VK_SUCCESS)
            throw std::runtime_error("Failed to create depth image view");
    }

    // ----- Framebuffer -----
    {
        std::array<VkImageView, 2> views = {imageView, depthImageView};

        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = renderPass;
        ci.attachmentCount = static_cast<uint32_t>(views.size());
        ci.pAttachments    = views.data();
        ci.width           = w;
        ci.height          = h;
        ci.layers          = 1;

        if (vkCreateFramebuffer(ctx.device, &ci, nullptr, &framebuffer) != VK_SUCCESS)
            throw std::runtime_error("Failed to create framebuffer");
    }

    // ----- Staging buffer (persistently mapped) -----
    {
        VkDeviceSize size = static_cast<VkDeviceSize>(w) * h * 4;

        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size  = size;
        bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;

        VmaAllocationCreateInfo ai{};
        ai.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        ai.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocInfo{};
        if (vmaCreateBuffer(m_allocator, &bci, &ai, &stagingBuffer, &stagingAlloc, &allocInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to create staging buffer");

        mappedPtr = allocInfo.pMappedData;
    }
}

void OffscreenTarget::DestroyTransientResources()
{
    if (m_allocator == VK_NULL_HANDLE) return;

    VmaAllocatorInfo info{};
    vmaGetAllocatorInfo(m_allocator, &info);
    VkDevice dev = info.device;

    if (stagingBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, stagingBuffer, stagingAlloc);
        stagingBuffer = VK_NULL_HANDLE;
        mappedPtr     = nullptr;
    }
    if (framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(dev, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;
    }
    if (depthImageView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, depthImage, depthAlloc);
        depthImage = VK_NULL_HANDLE;
    }
    if (imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(dev, imageView, nullptr);
        imageView = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, image, imageAlloc);
        image = VK_NULL_HANDLE;
    }
}

