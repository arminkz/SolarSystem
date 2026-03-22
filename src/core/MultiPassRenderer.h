#pragma once

#include "stdafx.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/Pipeline.h"
#include "vulkan/FrameBuffer.h"
#include "vulkan/RenderPass.h"
#include "vulkan/resources/TextureSampler.h"
#include "vulkan/resources/Buffer.h"
#include "vulkan/DescriptorSet.h"
#include "core/Renderer.h"
#include "scene/Camera.h"
#include "scene/SelectableModel.h"
#include "scene/Model.h"


class MultiPassRenderer : public Renderer
{
public:
    MultiPassRenderer(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<SwapChain> swapChain);
    ~MultiPassRenderer();

    void update() override final;
    virtual void advance() = 0;
    void recordToCommandBuffer(VkCommandBuffer commandBuffer, uint32_t targetSwapImageIndex) override;
    void onSwapChainRecreated() override;

    void handleMouseClick(float mouseX, float mouseY) override;
    void handleMouseDrag(float dx, float dy) override;
    void handleMouseWheel(float dy) override;
    void handleKeyDown(int key, int scancode, int modifiers) override = 0;

    const DescriptorSet* getSceneDescriptorSet() const { return _sceneDescriptorSets[_currentFrame].get(); }

    void setBloomSettings(float scale, float strength);

protected:
    // Scene UBO
    struct SceneInfo {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;
        alignas(4)  float time = 0.0f;
        alignas(16) glm::vec3 cameraPosition;
        alignas(16) glm::vec3 lightColor;
    } _sceneInfo;

    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> _sceneInfoUBOs;
    std::array<std::unique_ptr<DescriptorSet>, MAX_FRAMES_IN_FLIGHT> _sceneDescriptorSets;

    // Scene Models
    std::vector<std::shared_ptr<Model>> _sceneModels;
    std::unordered_map<int, std::shared_ptr<SelectableModel>> _selectableModels;

    struct GlowPassEntry {
        std::shared_ptr<Model> model;
        glm::vec3 glowColor{0.f, 0.f, 0.f};
        bool useGlowPipeline = true;
    };
    std::vector<GlowPassEntry> _glowPassModels;

    // Camera
    std::unique_ptr<Camera> _camera = nullptr;
    uint32_t _currentTargetObjectID = 0;

    // MSAA
    VkSampleCountFlagBits _msaaSamples;

    // Offscreen render pass (with MSAA)
    std::unique_ptr<RenderPass> _mainRenderPass;

private:
    // Render passes
    std::unique_ptr<RenderPass> _noMSAARenderPass;
    void createRenderPasses();

    // Framebuffers
    std::vector<std::unique_ptr<FrameBuffer>> _mainFrameBuffers; // for composite pass
    std::vector<std::unique_ptr<FrameBuffer>> _offscreenFrameBuffers; // [0]=glow  [1]=vertical blur  [2]=horizontal blur  [3]=scene
    void createFrameBuffers();


    // Glow / Blur pass
    struct GlowPassPushConstants {
        alignas(16) glm::mat4 model;
        alignas(16) glm::vec3 glowColor;
    };
    void drawWithGlowPipeline(VkCommandBuffer commandBuffer, const Model& model, glm::vec3 glowColor) const;

    struct BlurSettings {
        float blurScale    = 2.0f;
        float blurStrength = 1.5f;
    } _blurSettings;

    std::unique_ptr<Buffer> _blurSettingsUBO;
    std::unique_ptr<Pipeline> _glowPipeline;
    std::unique_ptr<Pipeline> _blurVertPipeline;
    std::unique_ptr<Pipeline> _blurHorizPipeline;
    std::unique_ptr<DescriptorSet> _blurVertDescriptorSet;
    std::unique_ptr<DescriptorSet> _blurHorizDescriptorSet;
    std::unique_ptr<TextureSampler> _ppTextureSampler;

    void createPostProcessPipelines();
    void createPostProcessDescriptors();


    // Composite pass
    std::unique_ptr<RenderPass> _compositeRenderPass;
    std::unique_ptr<Pipeline> _compositePipeline;
    std::unique_ptr<DescriptorSet> _compositeDescriptorSet;


    // Object selection
    struct ObjectSelectionPushConstants {
        alignas(16) glm::mat4 model;
        alignas(4)  uint32_t objectID;
    };

    std::unique_ptr<RenderPass> _objectSelectionRenderPass;
    std::unique_ptr<FrameBuffer> _objectSelectionFrameBuffer;
    std::unique_ptr<Pipeline> _objectSelectionPipeline;

    void createObjectSelectionPipeline();
    uint32_t querySelectionImage(float mouseX, float mouseY);
};
