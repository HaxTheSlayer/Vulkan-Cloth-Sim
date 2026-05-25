#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "VulkanContext.h"
#include "ClothSolver.h"
#include "ClothRenderer.h"

#include <iostream>
#include <chrono>

glm::vec3 cameraPos = glm::vec3(0.0f, 0.0f, 8.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

int main() {
    try {
        VulkanContext context(800, 800);
        ClothSolver solver(context, 32, 32, 0.1f, 2000.0f);
        Renderer renderer(context, solver);

        vk::raii::Device& device = context.getDevice();
        vk::raii::Queue& queue = context.getQueue();

        vk::FenceCreateInfo fenceInfo(vk::FenceCreateFlagBits::eSignaled);
        vk::raii::Fence inFlightFence(device, fenceInfo);

        vk::SemaphoreCreateInfo semaphoreInfo{};
        vk::raii::Semaphore imageAvailableSemaphore(device, semaphoreInfo);
        vk::raii::Semaphore renderFinishedSemaphore(device, semaphoreInfo);

        vk::CommandBufferAllocateInfo allocInfo(*context.getCommandPool(), vk::CommandBufferLevel::ePrimary, 1);
        vk::raii::CommandBuffer cmdBuffer = std::move(device.allocateCommandBuffers(allocInfo).front());

        auto lastTime = std::chrono::high_resolution_clock::now();

        float windDirX = 0.5f;     // Blowing slightly to the right
        float windDirY = 0.0f;
        float windDirZ = -1.0f;    // Blowing away from the camera
        float windStrength = 15.0f;
        float windSpeed = 5.0f;
        float windScale = 0.2f;
        float aeroDrag = 1.5f;

        while (!glfwWindowShouldClose(context.getWindow())) {
            glfwPollEvents();

            auto currentTime = std::chrono::high_resolution_clock::now();
            float deltaTime = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - lastTime).count();
            lastTime = currentTime;

            float cameraSpeed = 150.0f * deltaTime;

            // Keyboard polling
            if (glfwGetKey(context.getWindow(), GLFW_KEY_W) == GLFW_PRESS)
                cameraPos += cameraSpeed * cameraFront;
            if (glfwGetKey(context.getWindow(), GLFW_KEY_S) == GLFW_PRESS)
                cameraPos -= cameraSpeed * cameraFront;
            if (glfwGetKey(context.getWindow(), GLFW_KEY_A) == GLFW_PRESS)
                cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
            if (glfwGetKey(context.getWindow(), GLFW_KEY_D) == GLFW_PRESS)
                cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;

            double mouseXpos, mouseYpos;
            glfwGetCursorPos(context.getWindow(), &mouseXpos, &mouseYpos);

            // Map Screen Pixels to World Space
            // Normalize the coordinates to [-0.5, 0.5] and multiplies by a scalar to match the physical width/height of  camera view
            float screenWidth = 800.0f; 
            float screenHeight = 800.0f; 
            float globalTime = static_cast<float>(glfwGetTime());

            float worldMouseX = ((float)mouseXpos / screenWidth - 0.5f) * 10.0f;
            float worldMouseY = -((float)mouseYpos / screenHeight - 0.5f) * 10.0f;
            float worldMouseZ = 0.0f; // Assume we are grabbing the front plane of the cloth

            // Checking click state
            int state = glfwGetMouseButton(context.getWindow(), GLFW_MOUSE_BUTTON_LEFT);
            bool isMouseDown = (state == GLFW_PRESS);

            //Wait for GPU & Acquire Next Image
            if (device.waitForFences({ *inFlightFence }, vk::True, UINT64_MAX) != vk::Result::eSuccess) {
                throw std::runtime_error("failed to wait for fence!");
            }
            device.resetFences({ *inFlightFence });

            auto [result, imageIndex] = context.getSwapChain().acquireNextImage(UINT64_MAX, *imageAvailableSemaphore, nullptr);

            //Begin Command Buffer Recording
            cmdBuffer.reset();
            cmdBuffer.begin({});

            //Execute Physics (Compute Pass)
            solver.dispatchCompute(cmdBuffer, deltaTime, worldMouseX, worldMouseY, worldMouseZ, isMouseDown,
                globalTime, windDirX, windDirY, windDirZ, windStrength, windSpeed, windScale, aeroDrag);

            // Prepare for Drawing (Image Transition)
            vk::ImageMemoryBarrier barrier2(
                {},
                vk::AccessFlagBits::eColorAttachmentWrite,
                vk::ImageLayout::eUndefined,
                vk::ImageLayout::eColorAttachmentOptimal,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                context.getSwapChainImages()[imageIndex],
                { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
            );
            cmdBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput,
                {}, {}, {}, { barrier2 }
            );
            //Execute Graphics (Dynamic Rendering Pass)
            glm::mat4 view = glm::lookAt(cameraPos, glm::vec3(0.0f, 0.0f, 0.0f), cameraUp);
            glm::mat4 proj = glm::perspective(glm::radians(45.0f), 800.0f / 800.0f, 0.01f, 100.0f);
            proj[1][1] *= -1;
            glm::mat4 mvp = proj * view;

            vk::ClearValue clearColor;
            clearColor.color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});

            vk::RenderingAttachmentInfo attachmentInfo(
                *context.getSwapChainImageViews()[imageIndex],               // imageView
                vk::ImageLayout::eColorAttachmentOptimal,       // imageLayout
                vk::ResolveModeFlagBits::eNone,                 // resolveMode 
                nullptr,                                        // resolveImageView
                vk::ImageLayout::eUndefined,                    // resolveImageLayout 
                vk::AttachmentLoadOp::eClear,                   // loadOp
                vk::AttachmentStoreOp::eStore,                  // storeOp
                clearColor                                      // clearValue
            );
            vk::RenderingInfo renderingInfo(
                {},                                                         // flags 
                vk::Rect2D({ 0, 0 }, context.getSwapChainExtent()),         // renderArea
                1,                                                          // layerCount
                0,                                                          // viewMask 
                1,                                                          // colorAttachmentCount
                &attachmentInfo                                             // pColorAttachments
            );

            cmdBuffer.beginRendering(renderingInfo);
            renderer.recordDrawCommands(cmdBuffer, solver.getCurrentFrame(), mvp);
            cmdBuffer.endRendering();


            //Prepare for Presentation (Image Transition)
            vk::ImageMemoryBarrier barrier3(
                vk::AccessFlagBits::eColorAttachmentWrite,
                {},
                vk::ImageLayout::eColorAttachmentOptimal,
                vk::ImageLayout::ePresentSrcKHR,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                context.getSwapChainImages()[imageIndex],
                { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
            );
            cmdBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eColorAttachmentOutput, vk::PipelineStageFlagBits::eBottomOfPipe,
                {}, {}, {}, { barrier3 }
            );
            //End Command Buffer & Submit
            cmdBuffer.end();

            vk::PipelineStageFlags waitStages = vk::PipelineStageFlagBits::eColorAttachmentOutput;

            vk::SubmitInfo computeSubmitInfo(
                *imageAvailableSemaphore, // waitSemaphores
                waitStages,               // waitDstStageMask
                *cmdBuffer,               // commandBuffers
                *renderFinishedSemaphore  // signalSemaphores
            );
            queue.submit({ computeSubmitInfo }, *inFlightFence);

            //Present to Screen
            vk::PresentInfoKHR presentInfo(
                1, &*renderFinishedSemaphore,
                1, &*context.getSwapChain(),
                &imageIndex
            );
            queue.presentKHR(presentInfo);
            queue.waitIdle();
        }

        // 13. Device Wait Idle (Cleanup Preparation)
        device.waitIdle();

    }
    catch (const std::exception& e) {
        std::cerr << "Fatal Error: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}