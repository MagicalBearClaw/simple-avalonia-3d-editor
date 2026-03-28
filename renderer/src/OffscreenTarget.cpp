#include "OffscreenTarget.h"
#include "VulkanContext.h"

#include <stdexcept>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void createImage(VmaAllocator allocator,
                        uint32_t     w,
                        uint32_t     h,
                        VkFormat     format,
                        VkImageUsageFlags usage,
                        VkImage&     outImage,
                        VmaAllocation& outAlloc)
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
        throw std::runtime_error("Failed to create offscreen image");
}

// ---------------------------------------------------------------------------
// OffscreenTarget
// ---------------------------------------------------------------------------
OffscreenTarget::OffscreenTarget(VulkanContext& ctx, uint32_t w, uint32_t h)
{
    // Create VMA allocator — provide volk's dynamic function pointers
    VmaVulkanFunctions vkFuncs{};
    vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vkFuncs.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo ai{};
    ai.physicalDevice   = ctx.physicalDevice;
    ai.device           = ctx.device;
    ai.instance         = ctx.instance;
    ai.vulkanApiVersion = VK_API_VERSION_1_2;
    ai.pVulkanFunctions = &vkFuncs;

    if (vmaCreateAllocator(&ai, &allocator) != VK_SUCCESS)
        throw std::runtime_error("Failed to create VMA allocator");

    Create(ctx, w, h);
}

OffscreenTarget::~OffscreenTarget()
{
    Destroy();
    vmaDestroyAllocator(allocator);
}

void OffscreenTarget::Recreate(VulkanContext& ctx, uint32_t w, uint32_t h)
{
    vkDeviceWaitIdle(ctx.device);
    Destroy();
    Create(ctx, w, h);
}

void OffscreenTarget::Create(VulkanContext& ctx, uint32_t w, uint32_t h)
{
    width  = w;
    height = h;

    // ----- Color image -----
    createImage(allocator, w, h, kFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                image, imageAlloc);

    // ----- Image view -----
    {
        VkImageViewCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image    = image;
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format   = kFormat;
        ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        if (vkCreateImageView(ctx.device, &ci, nullptr, &imageView) != VK_SUCCESS)
            throw std::runtime_error("Failed to create offscreen image view");
    }

    // ----- Render pass -----
    {
        VkAttachmentDescription colorAttachment{};
        colorAttachment.format         = kFormat;
        colorAttachment.samples        = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{};
        colorRef.attachment = 0;
        colorRef.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments    = &colorRef;

        // Dependency: ensure color attachment writes are visible before transfer
        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments    = &colorAttachment;
        ci.subpassCount    = 1;
        ci.pSubpasses      = &subpass;
        ci.dependencyCount = 1;
        ci.pDependencies   = &dep;

        if (vkCreateRenderPass(ctx.device, &ci, nullptr, &renderPass) != VK_SUCCESS)
            throw std::runtime_error("Failed to create render pass");
    }

    // ----- Framebuffer -----
    {
        VkFramebufferCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        ci.renderPass      = renderPass;
        ci.attachmentCount = 1;
        ci.pAttachments    = &imageView;
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
        if (vmaCreateBuffer(allocator, &bci, &ai, &stagingBuffer, &stagingAlloc, &allocInfo) != VK_SUCCESS)
            throw std::runtime_error("Failed to create staging buffer");

        mappedPtr = allocInfo.pMappedData;
    }
}

void OffscreenTarget::Destroy()
{
    if (stagingBuffer) {
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAlloc);
        stagingBuffer = VK_NULL_HANDLE;
        mappedPtr     = nullptr;
    }
    if (framebuffer) {
        VmaAllocatorInfo info{};
        vmaGetAllocatorInfo(allocator, &info);
        VkDevice dev = info.device;

        vkDestroyFramebuffer(dev, framebuffer, nullptr);
        framebuffer = VK_NULL_HANDLE;

        vkDestroyRenderPass(dev, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;

        vkDestroyImageView(dev, imageView, nullptr);
        imageView = VK_NULL_HANDLE;

        vmaDestroyImage(allocator, image, imageAlloc);
        image = VK_NULL_HANDLE;
    }
}
