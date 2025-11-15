#pragma once 

#include "stdafx.h"
#include "VulkanContext.h"
#include "Scene.h"
#include "Pipeline.h"
#include "FrameBuffer.h"
#include "RenderPass.h"
#include "TextureSampler.h"
#include "models/Planet.h"
#include "models/Sun.h"
#include "models/Earth.h"
#include "models/Orbit.h"
#include "models/GlowSphere.h"
#include "models/SkyBox.h"


class SolarSystemScene : public Scene
{
public:
    SolarSystemScene(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<SwapChain> swapChain);
    ~SolarSystemScene();

    void update(uint32_t currentImage) override;
    void recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t targetSwapImageIndex) override;

    void handleMouseClick(float mouseX, float mouseY) override;
    void handleMouseDrag(float dx, float dy) override;
    void handleMouseWheel(float dy) override;
    void handleKeyDown(int key, int scancode, int modifiers) override;

    const DescriptorSet* getSceneDescriptorSet() const { return _sceneDescriptorSets[_currentFrame].get(); }

private:

    // Scene information (Global information that we need to pass to the shader)
    struct SceneInfo {
        alignas(16) glm::mat4 view;
        alignas(16) glm::mat4 projection;

        alignas(4) float time = 0.0f; // in seconds
        alignas(16) glm::vec3 cameraPosition;
        alignas(16) glm::vec3 lightColor;
    } _sceneInfo;
    std::array<std::unique_ptr<UniformBuffer<SceneInfo>>, MAX_FRAMES_IN_FLIGHT> _sceneInfoUBOs;
    std::array<std::unique_ptr<DescriptorSet>, MAX_FRAMES_IN_FLIGHT> _sceneDescriptorSets;

    // Time
    bool _isPaused = false;
    TimePoint _lastFrameTime = std::chrono::high_resolution_clock::now();

    // Camera
    std::unique_ptr<Camera> _camera = nullptr;
    uint32_t _currentTargetObjectID;

    // MSAA
    VkSampleCountFlagBits _msaaSamples;

    // Render passes
    std::unique_ptr<RenderPass> _renderPass;
    std::unique_ptr<RenderPass> _offscreenRenderPass;
    std::unique_ptr<RenderPass> _offscreenRenderPassMSAA;
    std::unique_ptr<RenderPass> _objectSelectionRenderPass;
    void createRenderPasses();

    // Framebuffers
    std::vector<std::unique_ptr<FrameBuffer>> _mainFrameBuffers;
    std::vector<std::unique_ptr<FrameBuffer>> _offscreenFrameBuffers;
    std::unique_ptr<FrameBuffer> _objectSelectionFrameBuffer;
    void createFrameBuffers();

    // Pipelines
    std::shared_ptr<Pipeline> _planetPipeline;
    std::shared_ptr<Pipeline> _orbitPipeline;
    std::shared_ptr<Pipeline> _glowSpherePipeline;
    std::shared_ptr<Pipeline> _skyBoxPipeline;
    std::shared_ptr<Pipeline> _sunPipeline;
    std::shared_ptr<Pipeline> _earthPipeline;

    std::unique_ptr<Pipeline> _glowPipeline;
    std::unique_ptr<Pipeline> _blurVertPipeline;
    std::unique_ptr<Pipeline> _blurHorizPipeline;
    std::unique_ptr<Pipeline> _compositePipeline;
    std::unique_ptr<Pipeline> _objectSelectionPipeline;
    void createPipelines();
    void connectPipelines();

    // Drawables (Models)
    std::vector<std::shared_ptr<Planet>> _planets;
    std::vector<std::unique_ptr<Orbit>> _orbits;
    std::vector<std::unique_ptr<GlowSphere>> _glowSpheres;
    std::unique_ptr<SkyBox> _skyBox;
    std::shared_ptr<Sun> _sun;
    std::unique_ptr<GlowSphere> _sunGlowSphere;
    std::shared_ptr<Earth> _earth;
    std::unordered_map<int, std::shared_ptr<SelectableModel>> _selectableObjects; // Selectable objects
    void createModels();

    // Texture Sampler for intermediate passes
    std::unique_ptr<TextureSampler> _ppTextureSampler;

    // Glow pass
    struct GlowPassPushConstants {
        alignas(16) glm::mat4 model;
        alignas(16) glm::vec3 glowColor;
    };
    
    // Blur pass
    struct BlurSettings {
        float blurScale = 2.0f;
        float blurStrength = 1.5f;
    } blurSettings;
    std::unique_ptr<UniformBuffer<BlurSettings>> _blurSettingsUBO;
    std::unique_ptr<DescriptorSet> _blurVertDescriptorSet;
    std::unique_ptr<DescriptorSet> _blurHorizDescriptorSet;

    // Composite pass
    std::unique_ptr<DescriptorSet> _compositeDescriptorSet;

    // Object selection
    struct ObjectSelectionPushConstants {
        alignas(16) glm::mat4 model;
        alignas(4) uint32_t objectID;
    };
    uint32_t querySelectionImage(float mouseX, float mouseY);

    // Planet Sizes
    const float sizeSun = 3.f;
    const float sizeMercury = 0.21f;
    const float sizeVenus = 0.48f;
    const float sizeEarth = 0.54f;
    const float sizeMoon = 0.1f;
    const float sizeMars = 0.36f;
    const float sizeJupiter = 0.8f;
    const float sizeSaturn = 0.7f;
    const float sizeSaturnRing = 1.2f * sizeSaturn;
    const float sizeUranus = 0.4f;
    const float sizeNeptune = 0.4f;
    const float sizePluto = 0.2f;

    // Orbit Radii
    const float orbitRadMercury = 20.f;
    const float orbitRadVenus = 26.f;
    const float orbitRadEarth = 32.f;
    const float orbitRadMoon = 2.f;
    const float orbitRadMars = 44.f;
    const float orbitRadJupiter = 94.f;
    const float orbitRadSaturn = 154.f;
    const float orbitRadUranus = 238.f;
    const float orbitRadNeptune = 278.f;
    const float orbitRadPluto = 310.f;

    // Angle offset (t = 0)
    const float orbitAtT0Mercury = 252.25f;
    const float orbitAtT0Venus = 181.97f;
    const float orbitAtT0Earth = 100.46f;
    const float orbitAtT0Moon = 0.f;
    const float orbitAtT0Mars = 355.45f; 
    const float orbitAtT0Jupiter = 34.40f; 
    const float orbitAtT0Saturn = 49.94f;
    const float orbitAtT0Uranus = 313.23f;
    const float orbitAtT0Neptune = 304.88f;
    const float orbitAtT0Pluto = 238.92f;

    // Orbit speeds (degrees per second)
    const float orbitSpeedMercury = 0.0040f;
    const float orbitSpeedVenus = 0.0016f;
    const float orbitSpeedEarth = 0.0009f;
    const float orbitSpeedMoon = 0.01f;
    const float orbitSpeedMars = 0.0005f;
    const float orbitSpeedJupiter = 0.00008f;
    const float orbitSpeedSaturn = 0.00003f;
    const float orbitSpeedUranus = 	0.00001f;
    const float orbitSpeedNeptune = 0.000006f;
    const float orbitSpeedPluto = 0.000004f;

    // Spin angles at t = 0 (degrees)
    const float spinAtT0Mercury = 0.f;
    const float spinAtT0Venus = 177.36f;
    const float spinAtT0Earth = 0.f;
    const float spinAtT0Moon = 0.f;
    const float spinAtT0Mars = 0.f;
    const float spinAtT0Jupiter = 0.f;
    const float spinAtT0Saturn = 0.f;
    const float spinAtT0Uranus = 97.77f;
    const float spinAtT0Neptune = 30.07f;
    const float spinAtT0Pluto = 122.53f;

    // Spin speed (degrees per second)
    const float spinSpeedMercury = 0.007f;
    const float spinSpeedVenus = -0.0017f;
    const float spinSpeedEarth = 0.041f;
    const float spinSpeedMoon = 0.0159f;
    const float spinSpeedMars = 0.40f;
    const float spinSpeedJupiter = 1.0096f;
    const float spinSpeedSaturn = 0.9397f;
    const float spinSpeedUranus = -0.6f;
    const float spinSpeedNeptune = 0.62f;
    const float spinSpeedPluto = -0.26f;


    //Misc
    void saveSceneState(const std::string& filename);
    void loadSceneState(const std::string& filename);

};


