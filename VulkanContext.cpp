#include "VulkanContext.h"
#include <iostream>
#include <stdexcept>
#include <set>
#include <algorithm>

namespace {
    const std::vector<const char*> validationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    // We need the swapchain extension for our logical device later
    const std::vector<const char*> requiredDeviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

#ifdef NDEBUG
    constexpr bool enableValidationLayers = false;
#else
    constexpr bool enableValidationLayers = true;
#endif

    VKAPI_ATTR vk::Bool32 VKAPI_CALL debugCallback(
        vk::DebugUtilsMessageSeverityFlagBitsEXT severity,
        vk::DebugUtilsMessageTypeFlagsEXT type,
        const vk::DebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void*)
    {
        if (severity >= vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning) {
            std::cerr << "Validation Layer: " << pCallbackData->pMessage << std::endl;
        }
        return vk::False;
    }
}

std::vector<const char*> VulkanContext::getRequiredInstanceExtensions() {
    uint32_t glfwExtensionCount = 0;
    auto     glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

    std::vector<const char*> extensions(glfwExtensions, glfwExtensions + glfwExtensionCount);
    if (enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    return extensions;
}

// Constructor implementation
VulkanContext::VulkanContext(uint32_t width, uint32_t height) {
    initWindow(width, height);
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createSwapChain();
    createImageViews();
    createCommandPool();
}

// Destructor implementation
VulkanContext::~VulkanContext() {
    // vk::raii handles most cleanup automatically in reverse order of creation!
    // We only need to manually clean up the C-style GLFW pointers here.
    if (window) {
        glfwDestroyWindow(window);
    }
    glfwTerminate();
}

void VulkanContext::initWindow(uint32_t width, uint32_t height) {
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window = glfwCreateWindow(width, height, "Vulkan Gravity Simulator", nullptr, nullptr);
}

void VulkanContext::createInstance()
{
    vk::ApplicationInfo appInfo(
        "Hello Triangle",           // pApplicationName
        VK_MAKE_VERSION(1, 0, 0),   // applicationVersion
        "No Engine",                // pEngineName
        VK_MAKE_VERSION(1, 0, 0),   // engineVersion
        vk::ApiVersion14            // apiVersion
    );

    // Get the required layers
    std::vector<char const*> requiredLayers;
    if (enableValidationLayers)
    {
        requiredLayers.assign(validationLayers.begin(), validationLayers.end());
    }

    // Check if the required layers are supported by the Vulkan implementation.
    auto layerProperties = context.enumerateInstanceLayerProperties();
    auto unsupportedLayerIt = std::ranges::find_if(requiredLayers,
        [&layerProperties](auto const& requiredLayer) {
            return std::ranges::none_of(layerProperties,
                [requiredLayer](auto const& layerProperty) { return strcmp(layerProperty.layerName, requiredLayer) == 0; });
        });
    if (unsupportedLayerIt != requiredLayers.end())
    {
        throw std::runtime_error("Required layer not supported: " + std::string(*unsupportedLayerIt));
    }

    // Get the required extensions.
    auto requiredExtensions = getRequiredInstanceExtensions();

    // Check if the required extensions are supported by the Vulkan implementation.
    auto extensionProperties = context.enumerateInstanceExtensionProperties();
    auto unsupportedPropertyIt =
        std::ranges::find_if(requiredExtensions,
            [&extensionProperties](auto const& requiredExtension) {
                return std::ranges::none_of(extensionProperties,
                    [requiredExtension](auto const& extensionProperty) { return strcmp(extensionProperty.extensionName, requiredExtension) == 0; });
            });
    if (unsupportedPropertyIt != requiredExtensions.end())
    {
        throw std::runtime_error("Required extension not supported: " + std::string(*unsupportedPropertyIt));
    }

    vk::InstanceCreateInfo createInfo(
        {},                                                 // flags
        &appInfo,                                           // pApplicationInfo
        static_cast<uint32_t>(requiredLayers.size()),       // enabledLayerCount
        requiredLayers.data(),                              // ppEnabledLayerNames
        static_cast<uint32_t>(requiredExtensions.size()),   // enabledExtensionCount
        requiredExtensions.data()                           // ppEnabledExtensionNames
    );
    instance = vk::raii::Instance(context, createInfo);
}

void VulkanContext::setupDebugMessenger() {
    if (!enableValidationLayers)
        return;

    vk::DebugUtilsMessageSeverityFlagsEXT severityFlags(
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError);

    vk::DebugUtilsMessageTypeFlagsEXT     messageTypeFlags(
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation);

    vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCreateInfoEXT(
        {},                 //flags
        severityFlags,      //mesageSeverity
        messageTypeFlags,   //messageType
        &debugCallback      //pfnUserCallBack
    );

    debugMessenger = instance.createDebugUtilsMessengerEXT(debugUtilsMessengerCreateInfoEXT);
}

void VulkanContext::createSurface() {
    VkSurfaceKHR _surface;
    if (glfwCreateWindowSurface(*instance, window, nullptr, &_surface) != 0)
    {
        throw std::runtime_error("failed to create window surface!");
    }
    surface = vk::raii::SurfaceKHR(instance, _surface);
}

bool VulkanContext::isDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice) {
    // Check if the physicalDevice supports the Vulkan 1.3 API version
    bool supportsVulkan1_3 = physicalDevice.getProperties().apiVersion >= VK_API_VERSION_1_3;

    // Check if any of the queue families support graphics operations
    auto queueFamilies = physicalDevice.getQueueFamilyProperties();
    bool supportsGraphics = std::ranges::any_of(queueFamilies, [](auto const& qfp) { return !!(qfp.queueFlags & vk::QueueFlagBits::eGraphics); });

    // Check for required extensions (like the Swapchain)
    auto availableExtensions = physicalDevice.enumerateDeviceExtensionProperties();
    bool supportsExtensions = std::ranges::all_of(requiredDeviceExtensions,
        [&](const char* reqExt) {
            return std::ranges::any_of(availableExtensions,
                [&](const auto& availExt) { return strcmp(availExt.extensionName, reqExt) == 0; });
        });

    // Check if the physicalDevice supports the required features 
    auto features = physicalDevice.template getFeatures2<vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
        vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>();
    bool supportsRequiredFeatures = features.template get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy &&
        features.template get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering &&
        features.template get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState &&
        features.template get<vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>().timelineSemaphore;

    // Return true if the physicalDevice meets all the criteria
    return supportsGraphics && supportsExtensions && supportsRequiredFeatures;
}

void VulkanContext::pickPhysicalDevice() {
    std::vector<vk::raii::PhysicalDevice> physicalDevices = instance.enumeratePhysicalDevices();
    auto const                            devIter = std::ranges::find_if(physicalDevices, [&](auto const& physicalDevice) { return isDeviceSuitable(physicalDevice); });
    if (devIter == physicalDevices.end())
    {
        throw std::runtime_error("failed to find a suitable GPU!");
    }
    physicalDevice = *devIter;
}

void VulkanContext::createLogicalDevice() {
    std::vector<vk::QueueFamilyProperties> queueFamilyProperties = physicalDevice.getQueueFamilyProperties();

    // get the first index into queueFamilyProperties which supports both graphics and present
    for (uint32_t qfpIndex = 0; qfpIndex < queueFamilyProperties.size(); qfpIndex++)
    {
        if ((queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eGraphics) &&
            (queueFamilyProperties[qfpIndex].queueFlags & vk::QueueFlagBits::eCompute) &&
            physicalDevice.getSurfaceSupportKHR(qfpIndex, *surface))
        {
            // found a queue family that supports both graphics and present
            queueIndex = qfpIndex;
            break;
        }
    }
    if (queueIndex == ~0)
    {
        throw std::runtime_error("Could not find a queue for graphics and present -> terminating");
    }

    vk::StructureChain<
        vk::PhysicalDeviceFeatures2,
        vk::PhysicalDeviceVulkan13Features,
        vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT,
        vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR> featureChain;

    featureChain.get<vk::PhysicalDeviceFeatures2>().features.samplerAnisotropy = true;
    featureChain.get<vk::PhysicalDeviceVulkan13Features>().synchronization2 = true;
    featureChain.get<vk::PhysicalDeviceVulkan13Features>().dynamicRendering = true;
    featureChain.get<vk::PhysicalDeviceExtendedDynamicStateFeaturesEXT>().extendedDynamicState = true;
    featureChain.get<vk::PhysicalDeviceTimelineSemaphoreFeaturesKHR>().timelineSemaphore = true;

    float queuePriority = 0.5f;
    vk::DeviceQueueCreateInfo queueCreateInfo(
        {}, queueIndex, 1, &queuePriority
    );

    vk::DeviceCreateInfo deviceCreateInfo(
        {},                                                         // flags
        1, &queueCreateInfo,                                        // queueCreateInfo
        0, nullptr,                                                 // enabledLayerCount (deprecated for device)
        static_cast<uint32_t>(requiredDeviceExtensions.size()),     // enabledExtensionCount
        requiredDeviceExtensions.data(),                            // ppEnabledExtensionNames
        nullptr                                                     // pEnabledFeatures (we use pNext chain instead)
    );
    deviceCreateInfo.pNext = &featureChain.get<vk::PhysicalDeviceFeatures2>();

    device = vk::raii::Device(physicalDevice, deviceCreateInfo);
    queue = vk::raii::Queue(device, queueIndex, 0);
}

void VulkanContext::createSwapChain() {
    vk::SurfaceCapabilitiesKHR surfaceCapabilities = physicalDevice.getSurfaceCapabilitiesKHR(*surface);

    //Size of Swapchain Image
    vk::Extent2D extent;
    if (surfaceCapabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
        extent = surfaceCapabilities.currentExtent;
    }
    else {
        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        extent = vk::Extent2D{
            std::clamp(static_cast<uint32_t>(width), surfaceCapabilities.minImageExtent.width, surfaceCapabilities.maxImageExtent.width),
            std::clamp(static_cast<uint32_t>(height), surfaceCapabilities.minImageExtent.height, surfaceCapabilities.maxImageExtent.height)
        };
    }
    swapChainExtent = extent;

    //Choose the number of images 
    uint32_t minImageCount = std::max(3u, surfaceCapabilities.minImageCount);
    if (surfaceCapabilities.maxImageCount > 0 && minImageCount > surfaceCapabilities.maxImageCount) {
        minImageCount = surfaceCapabilities.maxImageCount;
    }

    //Choose the color format 
    std::vector<vk::SurfaceFormatKHR> availableFormats = physicalDevice.getSurfaceFormatsKHR(*surface);
    swapChainSurfaceFormat = availableFormats[0];
    for (const auto& format : availableFormats) {
        if (format.format == vk::Format::eB8G8R8A8Srgb && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            swapChainSurfaceFormat = format;
            break;
        }
    }

    //Choose Present Mode 
    std::vector<vk::PresentModeKHR> availablePresentModes = physicalDevice.getSurfacePresentModesKHR(*surface);
    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;
    for (const auto& mode : availablePresentModes) {
        if (mode == vk::PresentModeKHR::eMailbox) {
            presentMode = mode;
            break;
        }
    }

    vk::SwapchainCreateInfoKHR createInfo(
        {},                                     // flags
        *surface,                               // surface
        minImageCount,                          // minImageCount
        swapChainSurfaceFormat.format,          // imageFormat
        swapChainSurfaceFormat.colorSpace,      // imageColorSpace
        swapChainExtent,                        // imageExtent
        1,                                      // imageArrayLayers
        vk::ImageUsageFlagBits::eColorAttachment, // imageUsage
        vk::SharingMode::eExclusive,            // imageSharingMode
        0, nullptr,                             // queueFamilyIndexCount, pQueueFamilyIndices
        surfaceCapabilities.currentTransform,   // preTransform
        vk::CompositeAlphaFlagBitsKHR::eOpaque, // compositeAlpha
        presentMode,                            // presentMode
        vk::True,                               // clipped
        nullptr                                 // oldSwapchain
    );

    swapChain = vk::raii::SwapchainKHR(device, createInfo);
    swapChainImages = swapChain.getImages();
}

void VulkanContext::createImageViews() {
    swapChainImageViews.clear();
    for (const auto& image : swapChainImages) {
        vk::ComponentMapping components(
            vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity,
            vk::ComponentSwizzle::eIdentity, vk::ComponentSwizzle::eIdentity
        );
        vk::ImageSubresourceRange subresourceRange(
            vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1
        );

        vk::ImageViewCreateInfo createInfo(
            {},                             // flags
            image,                          // image
            vk::ImageViewType::e2D,         // viewType
            swapChainSurfaceFormat.format,  // format
            components,                     // components
            subresourceRange                // subresourceRange
        );
        swapChainImageViews.emplace_back(device, createInfo);
    }
}

void VulkanContext::createCommandPool() {
    vk::CommandPoolCreateInfo poolInfo(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer, // flags
        queueIndex                                          // queueFamilyIndex
    );
    commandPool = vk::raii::CommandPool(device, poolInfo);
}

uint32_t VulkanContext::findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties) {
    vk::PhysicalDeviceMemoryProperties memProperties = physicalDevice.getMemoryProperties();

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }

    throw std::runtime_error("Failed to find suitable memory type!");
}