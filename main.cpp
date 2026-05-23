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

glm::vec3 cameraPos = glm::vec3(0.0f, 0.5f, 4.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp = glm::vec3(0.0f, 1.0f, 0.0f);

bool firstMouse = true;
float yaw = -90.0f;
float pitch = -45.0f;
float lastX = 800.0f / 2.0;
float lastY = 800.0f / 2.0;

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (firstMouse) {
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
    }

    float xoffset = xpos - lastX;
    float yoffset = lastY - ypos;
    lastX = xpos;
    lastY = ypos;

    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;

    yaw += xoffset;
    pitch += yoffset;

    if (pitch > 89.0f)  pitch = 89.0f;
    if (pitch < -89.0f) pitch = -89.0f;

    // Convert Spherical coordinates to Cartesian vector
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

int main() {
    try {
        VulkanContext context(800, 800);
        ClothSolver solver(context, 32, 32);
        Renderer renderer(context, solver);

        // Capture the mouse cursor
        //glfwSetInputMode(context.getWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        // Register the callback
        //glfwSetCursorPosCallback(context.getWindow(), mouse_callback);

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
            //solver.dispatchCompute(cmdBuffer, deltaTime);

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