#pragma once

#include "stdafx.h"
#include "VulkanHelper.h"
#include "geometry/Vertex.h"
#include "geometry/HostMesh.h"
#include "Camera.h"
#include "Pipeline.h"
#include "DescriptorSet.h"
#include "SwapChain.h"
#include "FrameBuffer.h"
#include "SolarSystemScene.h"

class Renderer {

public:
    Renderer(std::shared_ptr<VulkanContext> ctx);
    ~Renderer();

    void initialize();
    void drawFrame();
    void informFramebufferResized() { _framebufferResized = true; };

    SwapChain* getSwapChain() { return _swapChain.get(); };

    //Camera* getCamera() { return _camera.get(); };
    //GUI* getGUI() { return _gui.get(); };

    void handleMouseClick(float mouseX, float mouseY);
    void handleMouseDrag(float dx, float dy);
    void handleMouseWheel(float dy);
    void handleKeyDown(int key, int scancode, int modifiers);

private:
    std::shared_ptr<VulkanContext> _ctx;

    // Swapchain
    std::shared_ptr<SwapChain> _swapChain;

    // Scene
    std::unique_ptr<Scene> _scene = nullptr;

    // Called when the window is resized
    void invalidate();



    // GUI (ImGui)
    //std::unique_ptr<GUI> _gui = nullptr;

    //void recreateObjectSelectionResources(); 
 



    // Command buffers
    std::vector<VkCommandBuffer> _commandBuffers;
    void createCommandBuffers();

    // Sync objects
    std::vector<VkSemaphore> _imageAvailableSemaphores;
    std::vector<VkSemaphore> _renderFinishedSemaphores;
    std::vector<VkFence> _inFlightFences;
    void createSyncObjects();
    
    bool _framebufferResized = false;

    uint32_t _frameCounter = 0;
    uint32_t _imageCounter = 0;
};