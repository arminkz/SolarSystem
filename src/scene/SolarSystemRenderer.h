#pragma once 

#include "stdafx.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/Pipeline.h"
#include "vulkan/FrameBuffer.h"
#include "vulkan/RenderPass.h"
#include "vulkan/resources/TextureSampler.h"
#include "vulkan/resources/Buffer.h"
#include "core/Renderer.h"
#include "models/Planet.h"
#include "models/Sun.h"
#include "models/Earth.h"
#include "models/Orbit.h"
#include "models/GlowSphere.h"
#include "models/SkyBox.h"
#include "scene/Camera.h"


class SolarSystemRenderer : public Renderer
{
public:
    SolarSystemRenderer(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<SwapChain> swapChain);
    ~SolarSystemRenderer();

    void update(uint32_t currentImage) override;
    void buildUI() override;
    void recordToCommandBuffer(VkCommandBuffer commandBuffer, uint32_t targetSwapImageIndex) override;
    void onSwapChainRecreated() override;

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
    std::array<std::unique_ptr<Buffer>, MAX_FRAMES_IN_FLIGHT> _sceneInfoUBOs;
    std::array<std::unique_ptr<DescriptorSet>, MAX_FRAMES_IN_FLIGHT> _sceneDescriptorSets;

    // Time
    float _timeScale = 1.0f;
    bool _isPaused = false;
    TimePoint _lastFrameTime = std::chrono::high_resolution_clock::now();

    // UI state
    bool _showUI = false;

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
    std::shared_ptr<Pipeline> _ringPipeline;

    std::unique_ptr<Pipeline> _glowPipeline;
    std::unique_ptr<Pipeline> _blurVertPipeline;
    std::unique_ptr<Pipeline> _blurHorizPipeline;
    std::unique_ptr<Pipeline> _compositePipeline;
    std::unique_ptr<Pipeline> _objectSelectionPipeline;
    void createPipelines();
    void connectPipelines();

    // Drawables (Models)
    std::vector<std::shared_ptr<Planet>> _planets;
    std::vector<std::shared_ptr<Orbit>> _orbits;
    std::vector<std::shared_ptr<GlowSphere>> _glowSpheres;
    std::unique_ptr<SkyBox> _skyBox;
    std::shared_ptr<Sun> _sun;
    std::shared_ptr<GlowSphere> _sunGlowSphere;
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
    std::unique_ptr<Buffer> _blurSettingsUBO;
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

    // Orbit Radii (visually compressed — real scale would push outer planets off-screen)
    // Inner planets are tightly packed; large gap after Mars represents the asteroid belt
    const float orbitRadMercury =  16.f;
    const float orbitRadVenus   =  24.f;
    const float orbitRadEarth   =  34.f;
    const float orbitRadMoon    =   2.f;
    const float orbitRadMars    =  44.f;
    const float orbitRadJupiter =  90.f;  // ~46 unit gap after Mars
    const float orbitRadSaturn  = 126.f;
    const float orbitRadUranus  = 160.f;
    const float orbitRadNeptune = 192.f;
    const float orbitRadPluto   = 218.f;

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

    // Spin speed (degrees per second), scaled to match orbital speed ratio.
    // spinSpeed = orbitSpeed * (orbitalPeriodDays / siderealRotationDays)
    const float spinSpeedMercury =  0.006f;    // 87.97d orbit / 58.646d rotation
    const float spinSpeedVenus   = -0.0015f;   // retrograde: 224.70d / 243.0d
    const float spinSpeedEarth   =  0.329f;    // 365.25d / 1.0d
    const float spinSpeedMoon    =  0.01f;     // tidally locked: 27.32d / 27.32d
    const float spinSpeedMars    =  0.335f;    // 686.97d / 1.026d
    const float spinSpeedJupiter =  0.838f;    // 4332.6d / 0.4135d
    const float spinSpeedSaturn  =  0.727f;    // 10759d / 0.444d
    const float spinSpeedUranus  = -0.427f;    // retrograde: 30687d / 0.718d
    const float spinSpeedNeptune =  0.538f;    // 60190d / 0.671d
    const float spinSpeedPluto   = -0.057f;    // retrograde: 90560d / 6.387d

    // Orbital inclination relative to the ecliptic (degrees)
    const float orbitInclinationMercury =  7.00f;
    const float orbitInclinationVenus   =  3.39f;
    const float orbitInclinationEarth   =  0.00f;
    const float orbitInclinationMoon    =  5.14f;
    const float orbitInclinationMars    =  1.85f;
    const float orbitInclinationJupiter =  1.30f;
    const float orbitInclinationSaturn  =  2.49f;
    const float orbitInclinationUranus  =  0.77f;
    const float orbitInclinationNeptune =  1.77f;
    const float orbitInclinationPluto   = 17.14f;


    //Misc
    void saveSceneState(const std::string& filename);
    void loadSceneState(const std::string& filename);

};


