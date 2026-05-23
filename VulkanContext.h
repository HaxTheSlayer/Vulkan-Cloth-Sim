#pragma once
#include <vulkan/vulkan_raii.hpp>
#include <GLFW/glfw3.h>

class VulkanContext {
public:
	VulkanContext(uint32_t width, uint32_t height);
	~VulkanContext();

	vk::raii::Device& getDevice() { return device; }
	vk::raii::PhysicalDevice& getPhysicalDevice() { return physicalDevice; }
	vk::raii::Queue& getQueue() { return queue; }
	GLFWwindow* getWindow() { return window; }
	vk::raii::SurfaceKHR& getSurfaceKHR() { return surface; }
	const vk::SurfaceFormatKHR& getSwapChainSurfaceFormat() const { return swapChainSurfaceFormat; }
	vk::raii::SwapchainKHR& getSwapChain() { return swapChain; }
	vk::raii::CommandPool& getCommandPool() { return commandPool; }

	vk::Extent2D getSwapChainExtent() const { return swapChainExtent; }
	vk::Format getSwapChainFormat() const { return swapChainSurfaceFormat.format; }
	std::vector<vk::raii::ImageView>& getSwapChainImageViews() { return swapChainImageViews; }
	std::vector<vk::Image>& getSwapChainImages() { return swapChainImages; }
	uint32_t getQueueIndex() const { return queueIndex; }
	uint32_t findMemoryType(uint32_t typeFilter, vk::MemoryPropertyFlags properties);

private:
	GLFWwindow* window = nullptr;
	vk::raii::Context context;
	vk::raii::Instance instance = nullptr;
	vk::raii::DebugUtilsMessengerEXT debugMessenger = nullptr;
	vk::raii::SurfaceKHR surface = nullptr;
	vk::raii::PhysicalDevice physicalDevice = nullptr;
	vk::raii::Device device = nullptr;
	uint32_t queueIndex = ~0;
	vk::raii::Queue queue = nullptr;

	vk::raii::SwapchainKHR swapChain = nullptr;
	std::vector<vk::Image> swapChainImages;
	vk::SurfaceFormatKHR swapChainSurfaceFormat;
	vk::Extent2D swapChainExtent;
	std::vector<vk::raii::ImageView> swapChainImageViews;

	vk::raii::CommandPool commandPool = nullptr;

	void initWindow(uint32_t width, uint32_t height);
	void createInstance();
	void setupDebugMessenger();
	void createSurface();
	void pickPhysicalDevice();
	void createLogicalDevice();
	void createSwapChain();
	void createImageViews();
	void createCommandPool();

	bool isDeviceSuitable(const vk::raii::PhysicalDevice& physicalDevice);
	std::vector<const char*> getRequiredInstanceExtensions();
};

#pragma once
