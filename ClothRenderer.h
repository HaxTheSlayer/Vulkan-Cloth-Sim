#pragma once
#include "VulkanContext.h"
#include "ClothSolver.h"
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <glm/glm.hpp>

// Push constant for the 3D camera
struct GraphicsPushConstants {
    glm::mat4 mvp;
};

class Renderer {
public:
    Renderer(VulkanContext& context, const ClothSolver& solver);
    ~Renderer() = default;

    void recordDrawCommands(vk::raii::CommandBuffer& cmdBuffer, uint32_t currentFrame, const glm::mat4& cameraViewProj);

private:
    VulkanContext& vkContext;
    const ClothSolver& clothSolver;

    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline graphicsPipeline = nullptr;
    vk::raii::Buffer indexBuffer = nullptr;
    vk::raii::DeviceMemory indexMemory = nullptr;
    uint32_t indexCount = 0;

    void createGraphicsPipeline();
    void createIndexBuffer();

    static std::vector<char> readFile(const std::string& filename);
};
