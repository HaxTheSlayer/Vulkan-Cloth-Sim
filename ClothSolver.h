#pragma once
#include "VulkanContext.h"
#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>
#include <vector>

struct ParticleData {
    glm::vec4 position_pinned;
    glm::vec4 velocity;
};

struct PushConstants {
    float deltaTime;
    uint32_t gridWidth;
    uint32_t gridHeight;
    float k;
    float rest_length;
};

class ClothSolver {
public:
    ClothSolver(VulkanContext& context, uint32_t gridWidth, uint32_t gridHeight, float spacing, float springStiffness);
    ~ClothSolver() = default;

    void dispatchCompute(vk::raii::CommandBuffer& cmdBuffer, float deltaTime);
    uint32_t getCurrentFrame() const { return currentFrame; }
    uint32_t getGridWidth() const { return gridWidth; }
    uint32_t getGridHeight() const { return gridHeight; }
    vk::Buffer getOutputBuffer() const { return *storageBuffers[currentFrame]; }
    const std::vector<vk::raii::Buffer>& getStorageBuffers() const { return storageBuffers; }

private:
    float spacing = 0.1f;
    float springStiffness = 500.0f;
    VulkanContext& vkContext;
    uint32_t gridWidth;
    uint32_t gridHeight;
    uint32_t particleCount;
    uint32_t currentFrame = 0; //Max frames = 2

    vk::raii::Buffer uniformBuffer = nullptr;
    vk::raii::DeviceMemory uniformMemory = nullptr;
    void* mappedUniformMemory = nullptr;

    std::vector<vk::raii::Buffer> storageBuffers;
    std::vector<vk::raii::DeviceMemory> storageMemory;

    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::DescriptorPool descriptorPool = nullptr;
    std::vector<vk::raii::DescriptorSet> descriptorSets;

    vk::raii::PipelineLayout computePipelineLayout = nullptr;
    vk::raii::Pipeline computePipeline = nullptr;

    void createStorageBuffers();
    void createDescriptorSets();
    void createComputePipeline();

    static std::vector<char> readFile(const std::string& filename);
};
#pragma once
