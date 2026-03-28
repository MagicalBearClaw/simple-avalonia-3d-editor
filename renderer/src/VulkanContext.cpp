#include "VulkanContext.h"

#include <stdexcept>
#include <cstring>
#include <vector>
#include <cstdio>

// ---------------------------------------------------------------------------
// Debug callback
// ---------------------------------------------------------------------------
#ifndef NDEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
    VkDebugUtilsMessageTypeFlagsEXT             /*type*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void*                                       /*user*/)
{
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        std::fprintf(stderr, "[Vulkan] %s\n", data->pMessage);
    return VK_FALSE;
}

static VkDebugUtilsMessengerCreateInfoEXT makeDebugCI()
{
    VkDebugUtilsMessengerCreateInfoEXT ci{};
    ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    ci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    ci.pfnUserCallback = debugCallback;
    return ci;
}
#endif

// ---------------------------------------------------------------------------
// VulkanContext constructor
// ---------------------------------------------------------------------------
VulkanContext::VulkanContext()
{
    if (volkInitialize() != VK_SUCCESS)
        throw std::runtime_error("Failed to initialize volk");

    // ----- Instance -----
    {
        VkApplicationInfo appInfo{};
        appInfo.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName   = "RendererApp";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName        = "RendererEngine";
        appInfo.engineVersion      = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion         = VK_API_VERSION_1_2;

        std::vector<const char*> layers;
        std::vector<const char*> extensions;

#ifndef NDEBUG
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

        VkInstanceCreateInfo ci{};
        ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo        = &appInfo;
        ci.enabledLayerCount       = static_cast<uint32_t>(layers.size());
        ci.ppEnabledLayerNames     = layers.data();
        ci.enabledExtensionCount   = static_cast<uint32_t>(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();

#ifndef NDEBUG
        auto debugCI = makeDebugCI();
        ci.pNext = &debugCI;
#endif

        if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS)
            throw std::runtime_error("Failed to create VkInstance");

        volkLoadInstance(instance);
    }

    // ----- Debug messenger -----
#ifndef NDEBUG
    {
        auto ci = makeDebugCI();
        if (vkCreateDebugUtilsMessengerEXT(instance, &ci, nullptr, &debugMessenger) != VK_SUCCESS)
            throw std::runtime_error("Failed to create debug messenger");
    }
#endif

    // ----- Physical device -----
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (count == 0)
            throw std::runtime_error("No Vulkan physical devices found");

        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance, &count, devices.data());

        // Prefer discrete GPU
        for (auto pd : devices) {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(pd, &props);
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                physicalDevice = pd;
                break;
            }
        }
        if (physicalDevice == VK_NULL_HANDLE)
            physicalDevice = devices[0];
    }

    // ----- Queue family -----
    {
        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
        std::vector<VkQueueFamilyProperties> props(count);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, props.data());

        bool found = false;
        for (uint32_t i = 0; i < count; ++i) {
            if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                graphicsFamily = i;
                found = true;
                break;
            }
        }
        if (!found)
            throw std::runtime_error("No graphics queue family found");
    }

    // ----- Logical device -----
    {
        float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{};
        qci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = graphicsFamily;
        qci.queueCount       = 1;
        qci.pQueuePriorities = &priority;

        VkDeviceCreateInfo ci{};
        ci.sType                = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount = 1;
        ci.pQueueCreateInfos    = &qci;

        if (vkCreateDevice(physicalDevice, &ci, nullptr, &device) != VK_SUCCESS)
            throw std::runtime_error("Failed to create VkDevice");

        volkLoadDevice(device);
        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
    }

    // ----- VMA allocator -----
    {
        VmaVulkanFunctions vkFuncs{};
        vkFuncs.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vkFuncs.vkGetDeviceProcAddr   = vkGetDeviceProcAddr;

        VmaAllocatorCreateInfo ai{};
        ai.physicalDevice   = physicalDevice;
        ai.device           = device;
        ai.instance         = instance;
        ai.vulkanApiVersion = VK_API_VERSION_1_2;
        ai.pVulkanFunctions = &vkFuncs;

        if (vmaCreateAllocator(&ai, &allocator) != VK_SUCCESS)
            throw std::runtime_error("Failed to create VMA allocator");
    }

    // ----- Command pool -----
    {
        VkCommandPoolCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.queueFamilyIndex = graphicsFamily;
        ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device, &ci, nullptr, &commandPool) != VK_SUCCESS)
            throw std::runtime_error("Failed to create command pool");
    }
}

// ---------------------------------------------------------------------------
// VulkanContext destructor
// ---------------------------------------------------------------------------
VulkanContext::~VulkanContext()
{
    if (device) {
        vkDestroyCommandPool(device, commandPool, nullptr);
        vmaDestroyAllocator(allocator);
        vkDestroyDevice(device, nullptr);
    }
#ifndef NDEBUG
    if (debugMessenger)
        vkDestroyDebugUtilsMessengerEXT(instance, debugMessenger, nullptr);
#endif
    if (instance)
        vkDestroyInstance(instance, nullptr);
}
