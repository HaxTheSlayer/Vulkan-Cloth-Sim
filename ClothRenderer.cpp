#include "ClothRenderer.h"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <array>

Renderer::Renderer(VulkanContext& context, const ClothSolver& solver)
    : vkContext(context), clothSolver(solver)
{
    createGraphicsPipeline();
    createIndexBuffer();
}

void Renderer::createGraphicsPipeline() {
    std::vector <char> vert = readFile("shaders/vert.spv");
    std::vector <char> frag = readFile("shaders/frag.spv");

    vk::ShaderModuleCreateInfo vertInfo({}, vert.size(), reinterpret_cast<const uint32_t*>(vert.data()));
    vk::raii::ShaderModule vertModule(vkContext.getDevice(), vertInfo);

    vk::ShaderModuleCreateInfo fragInfo({}, frag.size(), reinterpret_cast<const uint32_t*>(frag.data()));
    vk::raii::ShaderModule fragModule(vkContext.getDevice(), fragInfo);

    vk::PipelineShaderStageCreateInfo shaderStages[] = {
        { {}, vk::ShaderStageFlagBits::eVertex, *vertModule, "main" },
        { {}, vk::ShaderStageFlagBits::eFragment, *fragModule, "main" }
    };

    vk::VertexInputBindingDescription bindingDescription{
        0,                                  //binding
        sizeof(ParticleData),               //stride
        vk::VertexInputRate::eVertex        //inputRate
    };

    std::array<vk::VertexInputAttributeDescription, 3> attributeDescriptions = {
        vk::VertexInputAttributeDescription{
            0,                                          //location
            0,                                          //binding
            vk::Format::eR32G32B32A32Sfloat,            //format
            0                                           //offset
        },
        vk::VertexInputAttributeDescription{
            1,                                          //location
            0,                                          //binding
            vk::Format::eR32G32B32A32Sfloat,            //format
            offsetof(ParticleData, velocity)            //offset
        },
        vk::VertexInputAttributeDescription{
            2,                                          //location
            0,                                          //binding
            vk::Format::eR32G32B32A32Sfloat,            //format
            offsetof(ParticleData, normal)              //offset
        }
    };
    vk::PipelineVertexInputStateCreateInfo   vertexInputInfo{
        {},                                                     // flags
        1,                                                      // vertexBindingDescriptionCount
        &bindingDescription,                                    // pVertexBindingDescriptions
        static_cast<uint32_t>(attributeDescriptions.size()),    // vertexAttributeDescriptionCount
        attributeDescriptions.data()                            // pVertexAttributeDescriptions
    };

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly{
        {},                                     // flags
        vk::PrimitiveTopology::eTriangleList,   // topology
        vk::False                               // primitiveRestartEnable
    };

    vk::PipelineViewportStateCreateInfo viewportState{
        {},         // flags
        1,          // viewportCount
        nullptr,    // pViewports 
        1,          // scissorCount
        nullptr     // pScissors
    };

    vk::PipelineRasterizationStateCreateInfo rasterizer(
        {},                             // flags
        vk::False,                      // depthClampEnable
        vk::False,                      // rasterizerDiscardEnable
        vk::PolygonMode::eFill,         // polygonMode
        vk::CullModeFlagBits::eNone,    // cullMode
        vk::FrontFace::eClockwise,      // frontFace
        vk::False,                      // depthBiasEnable
        0.0f,                           // depthBiasConstantFactor 
        0.0f,                           // depthBiasClamp 
        0.0f,                           // depthBiasSlopeFactor 
        1.0f                            // lineWidth
    );

    vk::PipelineMultisampleStateCreateInfo multisampling(
        {},                                 //flags
        vk::SampleCountFlagBits::e1,        //rasterizationSamples
        vk::False                           //sampleShadingEnable
    );


    vk::PipelineColorBlendAttachmentState colorBlendAttachment(
        vk::False,                                  //blendEnable
        vk::BlendFactor::eOne,                      //srcColorBlendFactor
        vk::BlendFactor::eOne,                      //dstColorBlendFactor
        vk::BlendOp::eAdd,                          //colorBlendOp
        vk::BlendFactor::eOneMinusSrcAlpha,         //srcAlphaBlendFactor
        vk::BlendFactor::eZero,                     //dstAlphaBlendFactor
        vk::BlendOp::eAdd,                          //alphaBlendOp
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA   //colorWriteMask
    );

    vk::PipelineColorBlendStateCreateInfo colorBlending(
        {},                         //flags
        vk::False,                  //logicOpEnable
        vk::LogicOp::eCopy,         //logicOp
        1,                          //attachmentCount
        &colorBlendAttachment       //pAttachments
    );

    vk::DynamicState dynamicStates[] = {
            vk::DynamicState::eViewport,
            vk::DynamicState::eScissor
    };

    vk::PipelineDynamicStateCreateInfo dynamicState(
        {},                     //flags
        2,                      //dynamicStateCount
        dynamicStates           //pDynamicStates
    );

    vk::PushConstantRange pushConstantRange(
        vk::ShaderStageFlagBits::eVertex,           // stageFlags
        0,                                          // offset
        sizeof(GraphicsPushConstants)               // size 
    );

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo(
        {},                     // flags
        0,                      // setLayoutCount
        nullptr,                // pSetLayouts
        1,                      // pushConstantRangeCount
        &pushConstantRange      // pPushConstantRanges
    );
    pipelineLayout = vk::raii::PipelineLayout(vkContext.getDevice(), pipelineLayoutInfo);

    vk::Format colorFormat = vkContext.getSwapChainSurfaceFormat().format;
    vk::PipelineRenderingCreateInfo renderingCreateInfo(
        0,
        1,
        &colorFormat,
        vk::Format::eUndefined,
        vk::Format::eUndefined
    );

    vk::GraphicsPipelineCreateInfo pipelineInfo(
        {},
        2,
        shaderStages,
        &vertexInputInfo,
        &inputAssembly,
        nullptr,
        &viewportState,
        &rasterizer,
        &multisampling,
        nullptr,
        &colorBlending,
        &dynamicState,
        *pipelineLayout
    );

    pipelineInfo.pNext = &renderingCreateInfo;

    graphicsPipeline = vk::raii::Pipeline(vkContext.getDevice(), nullptr, pipelineInfo);
}

void Renderer::createIndexBuffer()
{
    uint32_t gridWidth = clothSolver.getGridWidth();     //Get width from vkContext
    uint32_t gridHeight = clothSolver.getGridHeight();    //Get height from vkContext

    std::vector<uint32_t> indices;
    indices.reserve((gridWidth - 1) * (gridHeight - 1) * 6);

    for (uint32_t y = 0; y < gridHeight - 1; y++) {
        for (uint32_t x = 0; x < gridWidth - 1; x++) {
            uint32_t topLeft = y * gridWidth + x;
            uint32_t topRight = y * gridWidth + x + 1;
            uint32_t bottomLeft = (y + 1) * gridWidth + x;
            uint32_t bottomRight = (y + 1) * gridWidth + x + 1;

            // Triangle 1
            indices.push_back(topLeft);
            indices.push_back(bottomLeft);
            indices.push_back(topRight);

            // Triangle 2
            indices.push_back(topRight);
            indices.push_back(bottomLeft);
            indices.push_back(bottomRight);
        }
    }

    indexCount = static_cast<uint32_t>(indices.size());
    vk::DeviceSize bufferSize = sizeof(uint32_t) * indices.size();

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
    memcpy(dataStaging, indices.data(), (size_t)bufferSize);
    stagingBufferMemory.unmapMemory();


    vk::BufferCreateInfo indexInfo(
        {}, 
        bufferSize,
        vk::BufferUsageFlagBits::eIndexBuffer | vk::BufferUsageFlagBits::eTransferDst,
        vk::SharingMode::eExclusive
    );
    indexBuffer = vk::raii::Buffer(vkContext.getDevice(), indexInfo);

    auto indexReqs = indexBuffer.getMemoryRequirements();
    indexMemory = vk::raii::DeviceMemory(
        vkContext.getDevice(), 
        vk::MemoryAllocateInfo(
        indexReqs.size,
        vkContext.findMemoryType(indexReqs.memoryTypeBits,
            vk::MemoryPropertyFlagBits::eDeviceLocal))
    );
    indexBuffer.bindMemory(*indexMemory, 0);

    vk::raii::CommandBuffer cmd = std::move(
        vkContext.getDevice().allocateCommandBuffers({
            *vkContext.getCommandPool(),
            vk::CommandBufferLevel::ePrimary, 1 }).front()
            );
    cmd.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });
    cmd.copyBuffer(*stagingBuffer, *indexBuffer, vk::BufferCopy(0, 0, bufferSize));
    cmd.end();

    vkContext.getQueue().submit(vk::SubmitInfo(nullptr, nullptr, *cmd), nullptr);
    vkContext.getQueue().waitIdle();
}

void Renderer::recordDrawCommands(vk::raii::CommandBuffer& cmdBuffer, uint32_t currentFrame, const glm::mat4& cameraViewProj) {
    vk::Extent2D swapChainExtent = vkContext.getSwapChainExtent();

    cmdBuffer.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(swapChainExtent.width), static_cast<float>(swapChainExtent.height), 0.0f, 1.0f));
    cmdBuffer.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), swapChainExtent));
    cmdBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, *graphicsPipeline);

    glm::mat4 modelMatrix = glm::mat4(1.0f);
    GraphicsPushConstants pushData{ cameraViewProj, modelMatrix };

    cmdBuffer.pushConstants<GraphicsPushConstants>(
        *pipelineLayout,
        vk::ShaderStageFlagBits::eVertex,
        0,
        pushData
    );

    //Bind the Compute Shader's output as the Vertex Buffer
    vk::Buffer vertexBuffer = clothSolver.getOutputBuffer();
    vk::DeviceSize offset = 0;
    cmdBuffer.bindVertexBuffers(0, { vertexBuffer }, { offset });

    //Draw all the particles!
    cmdBuffer.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);
    cmdBuffer.drawIndexed(indexCount, 1, 0, 0, 0);
}

std::vector<char> Renderer::readFile(const std::string& filename) {
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