#include "ClothSolver.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <random>
#include <numbers>

ClothSolver::ClothSolver(VulkanContext& context, uint32_t w, uint32_t h)
    : vkContext(context), gridWidth(w), gridHeight(h), particleCount(w* h)
{

    createStorageBuffers();
    createDescriptorSets();
    createComputePipeline();
}

void ClothSolver::createStorageBuffers() {
    std::vector<ParticleData> initialParticles(particleCount);

    float spacing = 0.1f;  // world-space distance between particles

    for (uint32_t y = 0; y < gridHeight; y++) {
        for (uint32_t x = 0; x < gridWidth; x++) {
            uint32_t i = y * gridWidth + x;

            float px = (x - gridWidth / 2.0f) * spacing;
            float py = 0.0f;
            float pz = (y - gridHeight / 2.0f) * spacing;

            // Top row is pinned (w = 1.0), everything else is free (w = 0.0)
            float pinned = (y == 0) ? 1.0f : 0.0f;

            initialParticles[i].position_pinned = glm::vec4(px, py, pz, pinned);
            initialParticles[i].velocity = glm::vec4(0.0f);
        }
    }

    vk::DeviceSize bufferSize = sizeof(ParticleData) * particleCount;

    vk::BufferCreateInfo stagingBufferInfo(
        {},
        bufferSize,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::SharingMode::eExclusive
    );
    vk::raii::Buffer stagingBuffer(vkContext.getDevice(), stagingBufferInfo);

    vk::MemoryRequirements stagingMemReq = stagingBuffer.getMemoryRequirements();
    vk::MemoryAllocateInfo stagingAllocInfo(
        stagingMemReq.size,
        vkContext.findMemoryType(stagingMemReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent)
    );
    vk::raii::DeviceMemory stagingBufferMemory(vkContext.getDevice(), stagingAllocInfo);

    // Bind memory to the staging buffer!
    stagingBuffer.bindMemory(*stagingBufferMemory, 0);

    void* dataStaging = stagingBufferMemory.mapMemory(0, bufferSize);
    memcpy(dataStaging, initialParticles.data(), (size_t)bufferSize);
    stagingBufferMemory.unmapMemory();


    for (int i = 0; i < 2; i++) {
        vk::BufferCreateInfo bufferInfo(
            {},
            bufferSize,
            vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer,
            vk::SharingMode::eExclusive
        );
        vk::raii::Buffer buffer(vkContext.getDevice(), bufferInfo);

        vk::MemoryRequirements memReq = buffer.getMemoryRequirements();
        vk::MemoryAllocateInfo allocInfo(
            memReq.size,
            vkContext.findMemoryType(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal) // <-- STRICTLY eDeviceLocal
        );
        vk::raii::DeviceMemory memory(vkContext.getDevice(), allocInfo);

        buffer.bindMemory(*memory, 0);

        storageBuffers.emplace_back(std::move(buffer));
        storageMemory.emplace_back(std::move(memory));
    }

    vk::CommandBufferAllocateInfo cmdAllocInfo(
        *vkContext.getCommandPool(),
        vk::CommandBufferLevel::ePrimary,
        1
    );
    vk::raii::CommandBuffer cmdBuffer = std::move(vk::raii::CommandBuffers(vkContext.getDevice(), cmdAllocInfo).front());

    vk::CommandBufferBeginInfo beginInfo(vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    cmdBuffer.begin(beginInfo);

    vk::BufferCopy copyRegion(0, 0, bufferSize);

    // Copy staging data into both Frame 0 and Frame 1 buffers
    cmdBuffer.copyBuffer(*stagingBuffer, *storageBuffers[0], copyRegion);
    cmdBuffer.copyBuffer(*stagingBuffer, *storageBuffers[1], copyRegion);

    cmdBuffer.end();

    vk::SubmitInfo submitInfo(nullptr, nullptr, *cmdBuffer);
    vkContext.getQueue().submit(submitInfo, nullptr);
    vkContext.getQueue().waitIdle();

    // The staging buffer and its memory are automatically destroyed when they fall out of scope here.
}

void ClothSolver::createDescriptorSets() {
    std::array layoutBindings{
            vk::DescriptorSetLayoutBinding(0, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr),
            vk::DescriptorSetLayoutBinding(1, vk::DescriptorType::eStorageBuffer, 1, vk::ShaderStageFlagBits::eCompute, nullptr) };

    vk::DescriptorSetLayoutCreateInfo layoutInfo(
        {},                                                 //flags
        static_cast<uint32_t>(layoutBindings.size()),       //bindingCount
        layoutBindings.data()                               //pBindings
    );
    descriptorSetLayout = vk::raii::DescriptorSetLayout(vkContext.getDevice(), layoutInfo);

    std::array poolSize{
            vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 4) };
    vk::DescriptorPoolCreateInfo poolInfo(
        vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,       //flags
        2,                                                          //maxSets
        poolSize.size(),                                            //poolSizeCount
        poolSize.data()                                             //pPoolSizes
    );
    descriptorPool = vk::raii::DescriptorPool(vkContext.getDevice(), poolInfo);

    std::vector<vk::DescriptorSetLayout> layouts(2, *descriptorSetLayout);
    vk::DescriptorSetAllocateInfo allocInfo(
        *descriptorPool,        // descriptorPool
        2,                      // descriptorSetCount
        layouts.data()          // pSetLayouts (Pointer to the array!)
    );

    descriptorSets.clear();
    descriptorSets = vkContext.getDevice().allocateDescriptorSets(allocInfo);

    for (int i = 0; i < 2; i++)
    {
        int readIndex = i;
        int writeIndex = 1 - i;

        vk::DescriptorBufferInfo storageReadInfo(
            *storageBuffers[readIndex],     //buffer
            0,                              //offset
            VK_WHOLE_SIZE                   //range
        );

        vk::DescriptorBufferInfo storageWriteInfo(
            *storageBuffers[writeIndex],    //buffer
            0,                              //offset
            VK_WHOLE_SIZE                   //range
        );

        std::array descriptorWrites{
            vk::WriteDescriptorSet(
                *descriptorSets[i],                     //dstSet
                0,                                      //dstBinding
                0,                                      //dstArrayElement
                1,                                      //descriptorCount
                vk::DescriptorType::eStorageBuffer,     //descriptorType
                nullptr,                                //pImageInfo
                &storageReadInfo,                       //pBufferInfo
                nullptr                                 //pTexelBufferView
            ),
            vk::WriteDescriptorSet(
                *descriptorSets[i],                     //dstSet
                1,                                      //dstBinding
                0,                                      //dstArrayElement
                1,                                      //descriptorCount
                vk::DescriptorType::eStorageBuffer,     //descriptorType
                nullptr,                                //pImageInfo
                &storageWriteInfo,                       //pBufferInfo
                nullptr                                 //pTexelBufferView
            ),
        };
        vkContext.getDevice().updateDescriptorSets(descriptorWrites, {});
    }
}

void ClothSolver::createComputePipeline() {
    std::vector <char> file = readFile("shaders/slang.spv");
    vk::ShaderModuleCreateInfo createInfo(
        {},                                                 //flags
        file.size(),                                        //codeSize
        reinterpret_cast<const uint32_t*>(file.data())      //pcode
    );
    vk::raii::ShaderModule shaderModule{ vkContext.getDevice(), createInfo };

    vk::PipelineShaderStageCreateInfo computeShaderStageInfo(
        {},                                     //flag
        vk::ShaderStageFlagBits::eCompute,      //stage
        shaderModule,                           //module
        "main"                                  //pName
    );

    vk::PushConstantRange pushConstantRange(
        vk::ShaderStageFlagBits::eCompute,      // stageFlags
        0,                                      // offset
        sizeof(float) + 2 * sizeof(uint32_t)        // size (12 bytes total)
    );

    // 2. Add the Push Constant to the Pipeline Layout
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo(
        {},                                     // flags
        1,                                      // setLayoutCount
        &*descriptorSetLayout,                  // pSetLayouts
        1,                                      // pushConstantRangeCount
        &pushConstantRange                      // pPushConstantRanges
    );

    computePipelineLayout = vk::raii::PipelineLayout(vkContext.getDevice(), pipelineLayoutInfo);

    vk::ComputePipelineCreateInfo pipelineInfo(
        {},                                 //flags
        computeShaderStageInfo,             //stage
        *computePipelineLayout             //layout
    );
    computePipeline = vk::raii::Pipeline(vkContext.getDevice(), nullptr, pipelineInfo);
}

void ClothSolver::dispatchCompute(vk::raii::CommandBuffer& cmdBuffer, float deltaTime) {
    // 1. Match the GLSL memory layout exactly (8 bytes total)
    struct PushConstants {
        float deltaTime;
        uint32_t gridWidth;
        uint32_t gridHeight;
    } pushData{ deltaTime, gridWidth, gridHeight };

    cmdBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *computePipeline);

    // Bind the descriptor set for the current frame
    cmdBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        *computePipelineLayout,
        0,
        { *descriptorSets[currentFrame] },
        nullptr
    );

    // Push the 8 bytes of constant data
    cmdBuffer.pushConstants<PushConstants>(
        *computePipelineLayout,
        vk::ShaderStageFlagBits::eCompute,
        0,
        pushData
    );

    // Dispatch the compute workgroups
    uint32_t groupCount = (particleCount + 255) / 256;
    cmdBuffer.dispatch(groupCount, 1, 1);

    // 2. Insert the Memory Barrier
    // The compute shader just wrote to the "Write" buffer (1 - currentFrame)
    vk::BufferMemoryBarrier bufferBarrier(
        vk::AccessFlagBits::eShaderWrite,         // srcAccessMask: Compute shader writing
        vk::AccessFlagBits::eVertexAttributeRead, // dstAccessMask: Vertex Input reading
        vk::QueueFamilyIgnored,                   // srcQueueFamilyIndex
        vk::QueueFamilyIgnored,                   // dstQueueFamilyIndex
        *storageBuffers[1 - currentFrame],        // buffer (The one we just wrote to)
        0,                                        // offset
        VK_WHOLE_SIZE                             // size
    );

    cmdBuffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader, // srcStageMask
        vk::PipelineStageFlagBits::eVertexInput,   // dstStageMask
        {},                                        // dependencyFlags
        nullptr,                                   // memoryBarriers
        bufferBarrier,                             // bufferMemoryBarriers
        nullptr                                    // imageMemoryBarriers
    );

    // Swap the frame index so the next pass reads the newly written data
    currentFrame = 1 - currentFrame;
}

std::vector<char> ClothSolver::readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("failed to open file: " + filename);
    }
    std::vector<char> buffer(file.tellg());
    file.seekg(0, std::ios::beg);
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    file.close();
    return buffer;
}