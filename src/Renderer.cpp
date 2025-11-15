#include "Renderer.h"
#include "stdafx.h"
#include "VulkanHelper.h"
#include "Pipeline.h"
#include "GUI.h"
#include "loader/ObjLoader.h"
#include "geometry/Vertex.h"
#include "geometry/HostMesh.h"
#include "geometry/DeviceMesh.h"
#include "geometry/MeshFactory.h"

Renderer::Renderer(std::shared_ptr<VulkanContext> ctx)
    : _ctx(std::move(ctx))
{
}

Renderer::~Renderer()
{
    // Wait for any unfinished GPU tasks
    vkDeviceWaitIdle(_ctx->device);

    // //Destroy dummy texture
    // DeviceTexture::cleanupDummy();

    // _ctx = nullptr;
}

void Renderer::initialize()
{
    spdlog::info("Max Frames in flight: {}", MAX_FRAMES_IN_FLIGHT);

    // Create swap chain
    _swapChain = std::make_shared<SwapChain>(_ctx);

    // Initialize Scene
    _scene = std::make_unique<SolarSystemScene>(_ctx, _swapChain);
    
    // Create ImGUI
    // _gui = std::make_unique<GUI>(_ctx);
    // _gui->init(_swapChain->getSwapChainExtent().width, _swapChain->getSwapChainExtent().height);
    // _gui->initResources(_renderPass, _msaaSamples);

    createCommandBuffers();
    createSyncObjects();
}


void Renderer::invalidate()
{
    vkDeviceWaitIdle(_ctx->device);

    //recreateMainRenderResources();
    //recreateObjectSelectionResources();
    //_gui->init(_swapChain->getSwapChainExtent().width, _swapChain->getSwapChainExtent().height);
}

// void Renderer::recreateMainRenderResources()
// {
//     // Clean up framebuffers
//     for (size_t i = 0; i < _mainFrameBuffers.size(); i++) {
//         _mainFrameBuffers[i] = nullptr;
//     }

//     // Clean up Swapchain
//     _swapChain->cleanupSwapChain();

//     // Create new swapchain
//     _swapChain->createSwapChain();

//     // Create new framebuffers
//     createMainFrameBuffers();
// }


// void Renderer::recreateObjectSelectionResources()
// {
//     _objectSelectionFrameBuffer = nullptr;
//     createObjectSelectionFrameBuffer();
// }


void Renderer::createCommandBuffers() {
    // Allocate command buffer
    _commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = _ctx->commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = (uint32_t)_commandBuffers.size();

    if (vkAllocateCommandBuffers(_ctx->device, &allocInfo, _commandBuffers.data()) != VK_SUCCESS) {
        spdlog::error("Failed to allocate command buffers!");
    } else {
        spdlog::info("Command buffers allocated successfully");
    }
}


void Renderer::createSyncObjects() {
    _imageAvailableSemaphores.resize(_swapChain->getSwapChainImageCount());
    _renderFinishedSemaphores.resize(_swapChain->getSwapChainImageCount());
    _inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; //Initially signaled

    for (size_t i = 0; i < _swapChain->getSwapChainImageCount(); i++) {
        if (vkCreateSemaphore(_ctx->device, &semaphoreInfo, nullptr, &_imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(_ctx->device, &semaphoreInfo, nullptr, &_renderFinishedSemaphores[i]) != VK_SUCCESS) {
            spdlog::error("Failed to create semaphores for swap chain image {}!", i);
        }
    }

    for(size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateFence(_ctx->device, &fenceInfo, nullptr, &_inFlightFences[i]) != VK_SUCCESS) {
            spdlog::error("Failed to create fences for frame {}!", i);
        }
    }
}


void Renderer::drawFrame() {
    vkWaitForFences(_ctx->device, 1, &_inFlightFences[_frameCounter], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(_ctx->device, _swapChain->getSwapChain(), UINT64_MAX, _imageAvailableSemaphores[_imageCounter], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        invalidate();
        return;
    } else if (result != VK_SUCCESS) {
        spdlog::error("Failed to acquire swap chain image!");
        return;
    }

    // Reset the fence before drawing
    vkResetFences(_ctx->device, 1, &_inFlightFences[_frameCounter]);
    vkResetCommandBuffer(_commandBuffers[_frameCounter], 0);

    // Update ImGui frame
    // _gui->newFrame();

    // ImGui::SetNextWindowPos(ImVec2(30, 30), ImGuiCond_Always);
    // ImGui::SetNextWindowSize(ImVec2(200, 300), ImGuiCond_Always);
    // ImGui::SetNextWindowCollapsed(true, ImGuiCond_Once);
    // ImGui::Begin("General", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

    // // FPS
    // ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

    // ImGui::End();

    // GUI elements can be added here
    //_selectableObjects[_currentTargetObjectID]->drawGUI();

    // _gui->endFrame();
    // _gui->updateBuffers(_currentFrame);

    // Update uniform buffer
    // updateUniformBuffer(_currentFrame);

    // Update Scene
    _scene->update(_frameCounter);

    // Record command buffer
    _scene->recordCommandBuffer(_commandBuffers[_frameCounter], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {_imageAvailableSemaphores[_imageCounter]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &_commandBuffers[_frameCounter];

    VkSemaphore signalSemaphores[] = {_renderFinishedSemaphores[_imageCounter]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(_ctx->graphicsQueue, 1, &submitInfo, _inFlightFences[_frameCounter]) != VK_SUCCESS) {
        spdlog::error("Failed to submit draw command buffer!");
        return;
    }

    // Present the image to the swap chain (after the command buffer is done)
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapChains[] = {_swapChain->getSwapChain()};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapChains;
    presentInfo.pImageIndices = &imageIndex;
    presentInfo.pResults = nullptr; // Optional

    result = vkQueuePresentKHR(_ctx->presentQueue, &presentInfo);
    
    if(result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || _framebufferResized) {
        _framebufferResized = false;
        invalidate();
    } else if (result != VK_SUCCESS) {
        spdlog::error("Failed to present swap chain image!");
    }

    _frameCounter = (_frameCounter + 1) % MAX_FRAMES_IN_FLIGHT;
    _imageCounter = (_imageCounter + 1) % _swapChain->getSwapChainImageCount();
}


void Renderer::handleMouseClick(float mouseX, float mouseY) {
    // Handle mouse click events in the scene
    _scene->handleMouseClick(mouseX, mouseY);
}


void Renderer::handleMouseDrag(float dx, float dy) {
    // Handle mouse drag events in the scene
    _scene->handleMouseDrag(dx, dy);
}


void Renderer::handleMouseWheel(float dy) {
    // Handle mouse wheel events in the scene
    _scene->handleMouseWheel(dy);
}


void Renderer::handleKeyDown(int key, int scancode, int modifiers) {
    // Handle key down events in the scene
    _scene->handleKeyDown(key, scancode, modifiers);
}