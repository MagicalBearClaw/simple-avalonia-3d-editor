#pragma once

#include <volk.h>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>

struct VulkanContext {
    VkInstance       instance       = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice         device         = VK_NULL_HANDLE;
    VkQueue          graphicsQueue  = VK_NULL_HANDLE;
    uint32_t         graphicsFamily = 0;
    VkCommandPool    commandPool    = VK_NULL_HANDLE;
    VmaAllocator     allocator      = VK_NULL_HANDLE;

#ifndef NDEBUG
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
#endif

    VulkanContext();
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
};
