#include "SolarSystemRenderer.h"

#include "vulkan/resources/TextureSampler.h"
#include "vulkan/resources/TextureCubemap.h"
#include "core/AssetPath.h"
#include "geometry/MeshFactory.h"
#include "gui/FontAwesome.h"


SolarSystemRenderer::SolarSystemRenderer(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<SwapChain> swapChain)
    : Renderer(std::move(ctx), std::move(swapChain))
{
    // MSAA
    _msaaSamples = VulkanHelper::getMaxMsaaSampleCount(_ctx);

    // Initialize scene information
    _sceneInfo.view = glm::mat4(1.0f);
    _sceneInfo.projection = glm::mat4(1.0f);
    _sceneInfo.time = 0.0f;
    _sceneInfo.cameraPosition = glm::vec3(0.0f);
    _sceneInfo.lightColor = glm::vec3(1.0f, 1.0f, 1.0f);

    // Create UBO for scene information
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        _sceneInfoUBOs[i] = std::make_unique<Buffer>(_ctx, sizeof(SceneInfo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        _sceneDescriptorSets[i] = std::make_unique<DescriptorSet>(_ctx, std::vector<Descriptor>{
            Descriptor(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 1, _sceneInfoUBOs[i]->getDescriptorInfo())
        });
    }

    createRenderPasses();
    createFrameBuffers();
    createModels();
    createPipelines();
    connectPipelines();
    
    // Create camera
    _currentTargetObjectID = _sun->getID();
    CameraParams cameraParams{};
    cameraParams.target = _sun->getPosition();
    cameraParams.radius = glm::length(glm::vec3(0.f, 150.f, 130.f));
    cameraParams.initialForward = glm::normalize(glm::vec3(0.f, -150.f, -130.f));
    _camera = std::make_unique<Camera>(cameraParams);
}


SolarSystemRenderer::~SolarSystemRenderer()
{
    // Wait for any unfinished GPU tasks
    vkDeviceWaitIdle(_ctx->device);

    _planetPipeline = nullptr;
    _orbitPipeline = nullptr;
    _glowSpherePipeline = nullptr;
    _skyBoxPipeline = nullptr;
    _sunPipeline = nullptr;
    _earthPipeline = nullptr;
    _ringPipeline = nullptr;

    _renderPass = nullptr;
    _offscreenRenderPass = nullptr;
    _offscreenRenderPassMSAA = nullptr;
    _objectSelectionRenderPass = nullptr;
}


void SolarSystemRenderer::onSwapChainRecreated()
{
    vkDeviceWaitIdle(_ctx->device);
    _mainFrameBuffers.clear();
    _objectSelectionFrameBuffer.reset();
    createFrameBuffers();

    // Recreate post-process descriptor sets — they reference image views from the
    // offscreen framebuffers which were just destroyed and recreated above.
    VkDescriptorImageInfo glowOut{};
    glowOut.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    glowOut.imageView   = _offscreenFrameBuffers[0]->getResolveImageView();
    glowOut.sampler     = _ppTextureSampler->getSampler();

    _blurVertDescriptorSet = std::make_unique<DescriptorSet>(_ctx, std::vector<Descriptor>{
        Descriptor(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT, 1, _blurSettingsUBO->getDescriptorInfo()),
        Descriptor(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, glowOut),
    });

    VkDescriptorImageInfo blurVertOut{};
    blurVertOut.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    blurVertOut.imageView   = _offscreenFrameBuffers[1]->getResolveImageView();
    blurVertOut.sampler     = _ppTextureSampler->getSampler();

    _blurHorizDescriptorSet = std::make_unique<DescriptorSet>(_ctx, std::vector<Descriptor>{
        Descriptor(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         VK_SHADER_STAGE_FRAGMENT_BIT, 1, _blurSettingsUBO->getDescriptorInfo()),
        Descriptor(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, blurVertOut),
    });

    VkDescriptorImageInfo blurHorizOut{};
    blurHorizOut.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    blurHorizOut.imageView   = _offscreenFrameBuffers[2]->getResolveImageView();
    blurHorizOut.sampler     = _ppTextureSampler->getSampler();

    VkDescriptorImageInfo normalOut{};
    normalOut.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalOut.imageView   = _offscreenFrameBuffers[3]->getResolveImageView();
    normalOut.sampler     = _ppTextureSampler->getSampler();

    _compositeDescriptorSet = std::make_unique<DescriptorSet>(_ctx, std::vector<Descriptor>{
        Descriptor(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, blurHorizOut),
        Descriptor(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, normalOut),
    });
}


void SolarSystemRenderer::createPipelines()
{
    VkDescriptorSetLayout sceneDSL = _sceneDescriptorSets[0]->getDescriptorSetLayout();

    // Texture sampler for post-processing
    _ppTextureSampler = std::make_unique<TextureSampler>(_ctx, 1, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

    // Glow pass pipeline
    PipelineParams glowPassPipelineParams;
    glowPassPipelineParams.name = "GlowPassPipeline";
    glowPassPipelineParams.descriptorSetLayouts = {sceneDSL};
    glowPassPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GlowPassPushConstants)}};
    glowPassPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    glowPassPipelineParams.msaaSamples = _msaaSamples;
    _glowPipeline = std::make_unique<Pipeline>(_ctx, 
        AssetPath::getInstance()->get("spv/glow/glow_vert.spv"), 
        AssetPath::getInstance()->get("spv/glow/glow_frag.spv"), 
        glowPassPipelineParams);

    VkDescriptorImageInfo glowPassOutputTexture{};
    glowPassOutputTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    glowPassOutputTexture.imageView = _offscreenFrameBuffers[0]->getResolveImageView();
    glowPassOutputTexture.sampler = _ppTextureSampler->getSampler();


    // Blur pass pipeline (vertical)
    _blurSettingsUBO = std::make_unique<Buffer>(_ctx, sizeof(BlurSettings), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    _blurSettingsUBO->copyData(&blurSettings, sizeof(blurSettings));

    std::vector<Descriptor> blurVertDescriptors = {
        Descriptor(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, _blurSettingsUBO->getDescriptorInfo()),
        Descriptor(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, glowPassOutputTexture), // Glow texture
    };
    _blurVertDescriptorSet = std::make_unique<DescriptorSet>(_ctx, blurVertDescriptors);

    VkDescriptorImageInfo blurVertPassOutputTexture{};
    blurVertPassOutputTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    blurVertPassOutputTexture.imageView = _offscreenFrameBuffers[1]->getResolveImageView();
    blurVertPassOutputTexture.sampler = _ppTextureSampler->getSampler();

    PipelineParams blurPassPipelineParams {};
    blurPassPipelineParams.name = "BlurPassPipeline - Vertical";
    blurPassPipelineParams.vertexBindingDescription = std::nullopt;
    blurPassPipelineParams.vertexAttributeDescriptions = {};
    blurPassPipelineParams.cullMode = VK_CULL_MODE_NONE;
    blurPassPipelineParams.descriptorSetLayouts = { _blurVertDescriptorSet->getDescriptorSetLayout() }; 
    blurPassPipelineParams.pushConstantRanges = {};
    blurPassPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    blurPassPipelineParams.msaaSamples = _msaaSamples;
    int blurDirection = 0; // 0 for vertical
    VkSpecializationMapEntry blurDirectionMapEntry = {0, 0, sizeof(int)};
    blurPassPipelineParams.fragmentShaderSpecializationInfo = VkSpecializationInfo {1, &blurDirectionMapEntry, sizeof(int), &blurDirection};
    _blurVertPipeline = std::make_unique<Pipeline>(_ctx, 
        AssetPath::getInstance()->get("spv/blur/blur_vert.spv"), 
        AssetPath::getInstance()->get("spv/blur/blur_frag.spv"), 
        blurPassPipelineParams);


    // Blur pass pipeline (horizontal)
    std::vector<Descriptor> blurHorizDescriptors = {
        Descriptor(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, _blurSettingsUBO->getDescriptorInfo()),
        Descriptor(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, blurVertPassOutputTexture), // Blur vert texture
    };
    _blurHorizDescriptorSet = std::make_unique<DescriptorSet>(_ctx, blurHorizDescriptors);

    VkDescriptorImageInfo blurHorizPassOutputTexture{};
    blurHorizPassOutputTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    blurHorizPassOutputTexture.imageView = _offscreenFrameBuffers[2]->getResolveImageView();;
    blurHorizPassOutputTexture.sampler = _ppTextureSampler->getSampler();

    blurPassPipelineParams.name = "BlurPassPipeline - Horizontal";
    blurDirection = 1;     // 1 for horizontal
    blurPassPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    _blurHorizPipeline = std::make_unique<Pipeline>(_ctx, 
        AssetPath::getInstance()->get("spv/blur/blur_vert.spv"), 
        AssetPath::getInstance()->get("spv/blur/blur_frag.spv"), 
        blurPassPipelineParams);


    // Composite pass pipeline
    VkDescriptorImageInfo normalPassOutputTexture{};
    normalPassOutputTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    normalPassOutputTexture.imageView = _offscreenFrameBuffers[3]->getResolveImageView();
    normalPassOutputTexture.sampler = _ppTextureSampler->getSampler();

    std::vector<Descriptor> compositeDescriptors = {
        Descriptor(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, blurHorizPassOutputTexture),   // Blur texture
        Descriptor(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, normalPassOutputTexture),      // Normal scene texture
    };
    _compositeDescriptorSet = std::make_unique<DescriptorSet>(_ctx, compositeDescriptors);

    PipelineParams compositePipelineParams {};
    compositePipelineParams.name = "CompositePipeline";
    compositePipelineParams.vertexBindingDescription = std::nullopt; // This is a fullscreen triangle, so we don't need vertex binding description
    compositePipelineParams.vertexAttributeDescriptions = {};
    compositePipelineParams.cullMode = VK_CULL_MODE_NONE;
    compositePipelineParams.descriptorSetLayouts = { _compositeDescriptorSet->getDescriptorSetLayout() };
    compositePipelineParams.pushConstantRanges = {};
    compositePipelineParams.renderPass = _renderPass->getRenderPass();
    compositePipelineParams.msaaSamples = _msaaSamples;
    _compositePipeline = std::make_unique<Pipeline>(_ctx, 
        AssetPath::getInstance()->get("spv/composite/composite_vert.spv"), 
        AssetPath::getInstance()->get("spv/composite/composite_frag.spv"), 
        compositePipelineParams);

    
    // Planet pipeline
    PipelineParams planetPipelineParams;
    planetPipelineParams.name = "PlanetPipeline";
    planetPipelineParams.descriptorSetLayouts = {sceneDSL, _planets[0]->getDescriptorSet()->getDescriptorSetLayout()};
    planetPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    planetPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    planetPipelineParams.msaaSamples = _msaaSamples;
    _planetPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/planet/planet_vert.spv"),
        AssetPath::getInstance()->get("spv/planet/planet_frag.spv"),
        planetPipelineParams);

    // Ring pipeline (transparent, uses texture alpha)
    PipelineParams ringPipelineParams;
    ringPipelineParams.name = "RingPipeline";
    ringPipelineParams.descriptorSetLayouts = {sceneDSL, _planets[0]->getDescriptorSet()->getDescriptorSetLayout()};
    ringPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    ringPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    ringPipelineParams.msaaSamples = _msaaSamples;
    ringPipelineParams.blendEnable = true;
    ringPipelineParams.depthWrite = false;
    ringPipelineParams.cullMode = VK_CULL_MODE_NONE;
    _ringPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/ring/ring_vert.spv"),
        AssetPath::getInstance()->get("spv/ring/ring_frag.spv"),
        ringPipelineParams);

    // Orbit pipeline
    PipelineParams orbitPipelineParams;
    orbitPipelineParams.name = "OrbitPipeline";
    orbitPipelineParams.descriptorSetLayouts = {sceneDSL};
    orbitPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)}};
    orbitPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    orbitPipelineParams.msaaSamples = _msaaSamples;
    orbitPipelineParams.depthTest = true;
    orbitPipelineParams.depthWrite = false;
    _orbitPipeline = std::make_unique<Pipeline>(_ctx, 
        AssetPath::getInstance()->get("spv/orbit/orbit_vert.spv"), 
        AssetPath::getInstance()->get("spv/orbit/orbit_frag.spv"), 
        orbitPipelineParams);

    // GlowSphere pipeline
    PipelineParams glowSpherePipelineParams;
    glowSpherePipelineParams.name = "GlowSpherePipeline";
    glowSpherePipelineParams.descriptorSetLayouts = {sceneDSL, _glowSpheres[0]->getDescriptorSet()->getDescriptorSetLayout()};
    glowSpherePipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    glowSpherePipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    glowSpherePipelineParams.msaaSamples = _msaaSamples;
    glowSpherePipelineParams.depthTest = true;
    glowSpherePipelineParams.depthWrite = false;
    glowSpherePipelineParams.frontFace = VK_FRONT_FACE_CLOCKWISE;
    _glowSpherePipeline = std::make_unique<Pipeline>(_ctx, 
        AssetPath::getInstance()->get("spv/glowsphere/glowsphere_vert.spv"), 
        AssetPath::getInstance()->get("spv/glowsphere/glowsphere_frag.spv"), 
        glowSpherePipelineParams);

    // SkyBox pipeline
    PipelineParams skyBoxPipelineParams;
    skyBoxPipelineParams.name = "SkyBoxPipeline";
    skyBoxPipelineParams.descriptorSetLayouts = {sceneDSL, _skyBox->getDescriptorSet()->getDescriptorSetLayout()};
    skyBoxPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT , 0, sizeof(glm::mat4)}};
    skyBoxPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    skyBoxPipelineParams.msaaSamples = _msaaSamples;
    skyBoxPipelineParams.depthTest = true;
    skyBoxPipelineParams.depthWrite = false;
    skyBoxPipelineParams.frontFace = VK_FRONT_FACE_CLOCKWISE;
    _skyBoxPipeline = std::make_unique<Pipeline>(_ctx, 
        AssetPath::getInstance()->get("spv/skybox/skybox_vert.spv"), 
        AssetPath::getInstance()->get("spv/skybox/skybox_frag.spv"), 
        skyBoxPipelineParams);

    // Earth pipeline
    PipelineParams earthPipelineParams;
    earthPipelineParams.name = "EarthPipeline";
    earthPipelineParams.descriptorSetLayouts = {sceneDSL, _earth->getDescriptorSet()->getDescriptorSetLayout()};
    earthPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    earthPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    earthPipelineParams.msaaSamples = _msaaSamples;
    _earthPipeline = std::make_unique<Pipeline>(_ctx, 
        AssetPath::getInstance()->get("spv/earth/earth_vert.spv"), 
        AssetPath::getInstance()->get("spv/earth/earth_frag.spv"), 
        earthPipelineParams);

    // Sun pipeline
    PipelineParams sunPipelineParams;
    sunPipelineParams.name = "SunPipeline";
    sunPipelineParams.descriptorSetLayouts = {sceneDSL};
    sunPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    sunPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    sunPipelineParams.msaaSamples = _msaaSamples;
    _sunPipeline = std::make_unique<Pipeline>(_ctx, 
        AssetPath::getInstance()->get("spv/sun/sun_vert.spv"), 
        AssetPath::getInstance()->get("spv/sun/sun_frag.spv"), 
        sunPipelineParams);

    // Object selection pipeline
    PipelineParams objectSelectionPipelineParams;
    objectSelectionPipelineParams.name = "ObjectSelectionPipeline";
    objectSelectionPipelineParams.descriptorSetLayouts = {sceneDSL};
    objectSelectionPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectSelectionPushConstants)}};
    objectSelectionPipelineParams.renderPass = _objectSelectionRenderPass->getRenderPass();
    objectSelectionPipelineParams.blendEnable = false;
    _objectSelectionPipeline = std::make_unique<Pipeline>(_ctx, 
        AssetPath::getInstance()->get("spv/selection/select_vert.spv"), 
        AssetPath::getInstance()->get("spv/selection/select_frag.spv"), 
        objectSelectionPipelineParams);
}


void SolarSystemRenderer::connectPipelines()
{
    // Set the pipeline for the skybox
    _skyBox->setPipeline(_skyBoxPipeline);

    // Set the pipelines for all planets
    for (const auto& planet : _planets) {
        planet->setPipeline(_planetPipeline);
    }

    // Set the pipeline for orbits
    for (const auto& orbit : _orbits) {
        orbit->setPipeline(_orbitPipeline);
    }

    //Set the pipeline for glow spheres
    _sunGlowSphere->setPipeline(_glowSpherePipeline);
    for (const auto& glowSphere : _glowSpheres) {
        glowSphere->setPipeline(_glowSpherePipeline);
    }

    // Earth, sun and saturn ring have their own pipelines
    _sun->setPipeline(_sunPipeline);
    _earth->setPipeline(_earthPipeline);
    for (const auto& planet : _planets) {
        if (planet->getName() == "SaturnRing")
            planet->setPipeline(_ringPipeline);
    }
}


void SolarSystemRenderer::createModels()
{
    // Create host meshes
    HostMesh sphere = MeshFactory::createSphereMesh(1.f, 64, 64);
    HostMesh ring = MeshFactory::createAnnulusMesh(1.3f, 2.2f, 64);
    HostMesh quad =  MeshFactory::createQuadMesh(1.f, 1.f, true);
    HostMesh cube = MeshFactory::createCubeMesh(1.f, 1.f, 1.f);

    // Create device meshes (GPU resources)
    std::shared_ptr<DeviceMesh> sphereDMesh = std::make_shared<DeviceMesh>(_ctx, sphere);
    std::shared_ptr<DeviceMesh> ringDMesh = std::make_shared<DeviceMesh>(_ctx, ring);
    std::shared_ptr<DeviceMesh> quadDMesh = std::make_shared<DeviceMesh>(_ctx, quad);
    std::shared_ptr<DeviceMesh> cubeDMesh = std::make_shared<DeviceMesh>(_ctx, cube);

    // SkyBox
    std::shared_ptr<TextureCubemap> skyTexture = std::make_shared<TextureCubemap>(_ctx, 
        AssetPath::getInstance()->get("textures/skybox"), 
        VK_FORMAT_R8G8B8A8_SRGB);
    _skyBox = std::make_unique<SkyBox>(_ctx, "Skybox", cubeDMesh, skyTexture);

    // Sun
    _sun = std::make_shared<Sun>(_ctx, "Sun", sphereDMesh, sizeSun);
    _selectableObjects[_sun->getID()] = _sun;

    // Mercury
    std::shared_ptr<Texture2D> mercuryColorTexture = std::make_shared<Texture2D>(_ctx, 
        AssetPath::getInstance()->get("textures/mercury/8k_mercury.jpg"), 
        VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> mercury = std::make_shared<Planet>(_ctx, "Mercury", sphereDMesh, mercuryColorTexture,
        sizeMercury, orbitRadMercury, orbitAtT0Mercury, orbitSpeedMercury, spinAtT0Mercury, spinSpeedMercury, orbitInclinationMercury);
    _selectableObjects[mercury->getID()] = mercury;
    _planets.push_back(mercury);

    // Venus
    std::shared_ptr<Texture2D> venusColorTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/venus/4k_venus_atmosphere.jpg"),
        VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> venus = std::make_shared<Planet>(_ctx, "Venus", sphereDMesh, venusColorTexture,
        sizeVenus, orbitRadVenus, orbitAtT0Venus, orbitSpeedVenus, spinAtT0Venus, spinSpeedVenus, orbitInclinationVenus);
    auto venusGlow = std::make_shared<GlowSphere>(_ctx, "VenusGlow", sphereDMesh, glm::vec4(0.74f, 0.69f, 0.2f, 1.f), 3.f, 4.f, sizeVenus * 1.03f, false);
    _glowSpheres.push_back(venusGlow);
    _selectableObjects[venus->getID()] = venus;
    _planets.push_back(venus);

    // Earth
    std::shared_ptr<Texture2D> colorTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/earth/10k_earth_day.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Texture2D> unlitTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/earth/10k_earth_night.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Texture2D> normalTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/earth/2k_earth_normal.png"), VK_FORMAT_R8G8B8A8_UNORM);
    std::shared_ptr<Texture2D> specularTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/earth/2k_earth_specular.jpeg"), VK_FORMAT_R8G8B8A8_UNORM);
    std::shared_ptr<Texture2D> overlayTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/earth/8k_earth_clouds.png"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Earth> earth = std::make_shared<Earth>(_ctx, "Earth", sphereDMesh, colorTexture, unlitTexture, normalTexture, specularTexture, overlayTexture,
         sizeEarth, orbitRadEarth, orbitAtT0Earth, orbitSpeedEarth, spinAtT0Earth, spinSpeedEarth, orbitInclinationEarth);
    _earth = earth;
    auto earthGlow = std::make_shared<GlowSphere>(_ctx, "EarthGlow", sphereDMesh, glm::vec4(0.45f, 0.55f, 1.f, 1.f), 3.f, 4.f, sizeEarth * 1.03f, false);
    _glowSpheres.push_back(earthGlow);
    _selectableObjects[earth->getID()] = earth;
    _planets.push_back(earth);

    // Earth Moon
    std::shared_ptr<Texture2D> moonColorTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/moon/8k_moon.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> moon = std::make_shared<Planet>(_ctx, "Moon", sphereDMesh, moonColorTexture,
        sizeMoon, orbitRadMoon, orbitAtT0Moon, orbitSpeedMoon, spinAtT0Moon, spinSpeedMoon, orbitInclinationMoon);
    _selectableObjects[moon->getID()] = moon;
    _planets.push_back(moon);

    // Mars
    std::shared_ptr<Texture2D> marsColorTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/mars/8k_mars.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> mars = std::make_shared<Planet>(_ctx, "Mars", sphereDMesh, marsColorTexture,
        sizeMars, orbitRadMars, orbitAtT0Mars, orbitSpeedMars, spinAtT0Mars, spinSpeedMars, orbitInclinationMars);
    _selectableObjects[mars->getID()] = mars;
    _planets.push_back(mars);

    // Jupiter
    std::shared_ptr<Texture2D> jupiterColorTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/jupiter/4k_jupiter.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> jupiter = std::make_shared<Planet>(_ctx, "Jupiter", sphereDMesh, jupiterColorTexture,
        sizeJupiter, orbitRadJupiter, orbitAtT0Jupiter, orbitSpeedJupiter, spinAtT0Jupiter, spinSpeedJupiter, orbitInclinationJupiter);
    _selectableObjects[jupiter->getID()] = jupiter;
    _planets.push_back(jupiter);

    // Saturn
    std::shared_ptr<Texture2D> saturnColorTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/saturn/8k_saturn.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> saturn = std::make_shared<Planet>(_ctx, "Saturn", sphereDMesh, saturnColorTexture,
        sizeSaturn, orbitRadSaturn, orbitAtT0Saturn, orbitSpeedSaturn, spinAtT0Saturn, spinSpeedSaturn, orbitInclinationSaturn);
    _selectableObjects[saturn->getID()] = saturn;
    _planets.push_back(saturn);

    // Saturn Ring
    std::shared_ptr<Texture2D> ringTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/saturn/8k_saturn_ring_alpha.png"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> saturn_ring = std::make_shared<Planet>(_ctx, "SaturnRing", ringDMesh, ringTexture,
        sizeSaturnRing, orbitRadSaturn, orbitAtT0Saturn, orbitSpeedSaturn, 0.f, 0.f, orbitInclinationSaturn);
    _planets.push_back(saturn_ring);

    // Uranus
    std::shared_ptr<Texture2D> uranusColorTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/uranus/1k_uranus.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> uranus = std::make_shared<Planet>(_ctx, "Uranus", sphereDMesh, uranusColorTexture,
        sizeUranus, orbitRadUranus, orbitAtT0Uranus, orbitSpeedUranus, spinAtT0Uranus, spinSpeedUranus, orbitInclinationUranus);
    _selectableObjects[uranus->getID()] = uranus;
    _planets.push_back(uranus);

    // Neptune
    std::shared_ptr<Texture2D> neptuneColorTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/neptune/2k_neptune.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> neptune = std::make_shared<Planet>(_ctx, "Neptune", sphereDMesh, neptuneColorTexture,
        sizeNeptune, orbitRadNeptune, orbitAtT0Neptune, orbitSpeedNeptune, spinAtT0Neptune, spinSpeedNeptune, orbitInclinationNeptune);
    _selectableObjects[neptune->getID()] = neptune;
    _planets.push_back(neptune);

    // Pluto
    std::shared_ptr<Texture2D> plutoColorTexture = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/pluto/2k_pluto.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> pluto = std::make_shared<Planet>(_ctx, "Pluto", sphereDMesh, plutoColorTexture,
        sizePluto, orbitRadPluto, orbitAtT0Pluto, orbitSpeedPluto, spinAtT0Pluto, spinSpeedPluto, orbitInclinationPluto);
    _selectableObjects[pluto->getID()] = pluto;
    _planets.push_back(pluto);


    auto mercuryOrbit = std::make_shared<Orbit>(_ctx, "MercuryOrbit", quadDMesh, orbitRadMercury, orbitAtT0Mercury, orbitSpeedMercury, orbitInclinationMercury);
    auto venusOrbit   = std::make_shared<Orbit>(_ctx, "VenusOrbit",   quadDMesh, orbitRadVenus,   orbitAtT0Venus,   orbitSpeedVenus,   orbitInclinationVenus);
    auto earthOrbit   = std::make_shared<Orbit>(_ctx, "EarthOrbit",   quadDMesh, orbitRadEarth,   orbitAtT0Earth,   orbitSpeedEarth,   orbitInclinationEarth);
    auto moonOrbit    = std::make_shared<Orbit>(_ctx, "MoonOrbit",    quadDMesh, orbitRadMoon,    orbitAtT0Moon,    orbitSpeedMoon,    orbitInclinationMoon);
    auto marsOrbit    = std::make_shared<Orbit>(_ctx, "MarsOrbit",    quadDMesh, orbitRadMars,    orbitAtT0Mars,    orbitSpeedMars,    orbitInclinationMars);
    auto jupiterOrbit = std::make_shared<Orbit>(_ctx, "JupiterOrbit", quadDMesh, orbitRadJupiter, orbitAtT0Jupiter, orbitSpeedJupiter, orbitInclinationJupiter);
    auto saturnOrbit  = std::make_shared<Orbit>(_ctx, "SaturnOrbit",  quadDMesh, orbitRadSaturn,  orbitAtT0Saturn,  orbitSpeedSaturn,  orbitInclinationSaturn);
    auto uranusOrbit  = std::make_shared<Orbit>(_ctx, "UranusOrbit",  quadDMesh, orbitRadUranus,  orbitAtT0Uranus,  orbitSpeedUranus,  orbitInclinationUranus);
    auto neptuneOrbit = std::make_shared<Orbit>(_ctx, "NeptuneOrbit", quadDMesh, orbitRadNeptune, orbitAtT0Neptune, orbitSpeedNeptune, orbitInclinationNeptune);
    auto plutoOrbit   = std::make_shared<Orbit>(_ctx, "PlutoOrbit",   quadDMesh, orbitRadPluto,   orbitAtT0Pluto,   orbitSpeedPluto,   orbitInclinationPluto);
    _orbits = { mercuryOrbit, venusOrbit, earthOrbit, moonOrbit, marsOrbit,
                jupiterOrbit, saturnOrbit, uranusOrbit, neptuneOrbit, plutoOrbit };

    _sunGlowSphere = std::make_shared<GlowSphere>(_ctx, "SunGlow", sphereDMesh, glm::vec4(1.f, 0.4f, 0.0f, 0.4f), 0.5f, 3.0f, sizeSun * 2.f, true);

    // Wire scene graph hierarchy
    _sun->addChild(_sunGlowSphere);
    _sun->addChild(mercuryOrbit);   _sun->addChild(_planets[0]);  // mercury
    _sun->addChild(venusOrbit);     _sun->addChild(_planets[1]);  // venus
    venus->addChild(venusGlow);
    _sun->addChild(earthOrbit);     _sun->addChild(_earth);
    _earth->addChild(earthGlow);
    _earth->addChild(moonOrbit);    _earth->addChild(_planets[3]); // moon
    _sun->addChild(marsOrbit);      _sun->addChild(_planets[4]);   // mars
    _sun->addChild(jupiterOrbit);   _sun->addChild(_planets[5]);   // jupiter
    _sun->addChild(saturnOrbit);    _sun->addChild(_planets[6]);   // saturn
    _sun->addChild(_planets[7]);    // saturn ring (same orbit as saturn)
    _sun->addChild(uranusOrbit);    _sun->addChild(_planets[8]);   // uranus
    _sun->addChild(neptuneOrbit);   _sun->addChild(_planets[9]);   // neptune
    _sun->addChild(plutoOrbit);     _sun->addChild(_planets[10]);  // pluto
}


void SolarSystemRenderer::createRenderPasses() {
    
    RenderPassParams offscreenRenderPassParams;
    offscreenRenderPassParams.name = "Offscreen Renderpass - No MSAA";
    offscreenRenderPassParams.colorFormat = VK_FORMAT_R8G8B8A8_SRGB;
    offscreenRenderPassParams.depthFormat = VulkanHelper::findDepthFormat(_ctx);
    offscreenRenderPassParams.colorAttachmentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    offscreenRenderPassParams.depthAttachmentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    offscreenRenderPassParams.useColor = true;
    offscreenRenderPassParams.useDepth = true;
    offscreenRenderPassParams.useResolve = false;
    offscreenRenderPassParams.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    _offscreenRenderPass = std::make_unique<RenderPass>(_ctx, offscreenRenderPassParams);

    offscreenRenderPassParams.name = "Offscreen Renderpass - MSAA";
    offscreenRenderPassParams.resolveFormat = VK_FORMAT_R8G8B8A8_SRGB;
    offscreenRenderPassParams.colorAttachmentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    offscreenRenderPassParams.resolveAttachmentLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    offscreenRenderPassParams.useResolve = true;
    offscreenRenderPassParams.msaaSamples = _msaaSamples;
    _offscreenRenderPassMSAA = std::make_unique<RenderPass>(_ctx, offscreenRenderPassParams);

    RenderPassParams screenRenderPassParams;
    screenRenderPassParams.name = "Onscreen Renderpass";
    screenRenderPassParams.colorFormat = _swapChain->getSwapChainImageFormat();
    screenRenderPassParams.depthFormat = VulkanHelper::findDepthFormat(_ctx);
    screenRenderPassParams.resolveFormat = _swapChain->getSwapChainImageFormat();
    screenRenderPassParams.colorAttachmentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    screenRenderPassParams.depthAttachmentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    screenRenderPassParams.resolveAttachmentLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    screenRenderPassParams.useColor = true;
    screenRenderPassParams.useDepth = true;
    screenRenderPassParams.useResolve = true;
    screenRenderPassParams.msaaSamples = _msaaSamples;
    screenRenderPassParams.isMultiPass = false;
    _renderPass = std::make_unique<RenderPass>(_ctx, screenRenderPassParams);

    RenderPassParams objectSelectionRenderPassParams;
    objectSelectionRenderPassParams.name = "Object Selection Renderpass";
    objectSelectionRenderPassParams.colorFormat = VK_FORMAT_R32_UINT;
    objectSelectionRenderPassParams.depthFormat = VulkanHelper::findDepthFormat(_ctx);
    objectSelectionRenderPassParams.colorAttachmentLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    objectSelectionRenderPassParams.depthAttachmentLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    objectSelectionRenderPassParams.useColor = true;
    objectSelectionRenderPassParams.useDepth = true;
    objectSelectionRenderPassParams.useResolve = false;
    objectSelectionRenderPassParams.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    objectSelectionRenderPassParams.isMultiPass = false;
    _objectSelectionRenderPass = std::make_unique<RenderPass>(_ctx, objectSelectionRenderPassParams);

}


void SolarSystemRenderer::createFrameBuffers() {
    
    _offscreenFrameBuffers.resize(4); // 4 offscreen framebuffers (1 for glow, 1 for vertical blur, 1 for horizontal blur, 1 for normal rendering)

    // Offscreen Framebuffers (No MSAA)
    FrameBufferParams offscreenFrameBufferParams{};
    offscreenFrameBufferParams.extent.width = static_cast<int>(_swapChain->getSwapChainExtent().width);
    offscreenFrameBufferParams.extent.height = static_cast<int>(_swapChain->getSwapChainExtent().height);
    offscreenFrameBufferParams.renderPass = _offscreenRenderPass->getRenderPass();
    offscreenFrameBufferParams.hasColor = true;
    offscreenFrameBufferParams.hasDepth = true;
    offscreenFrameBufferParams.hasResolve = false;
    offscreenFrameBufferParams.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    offscreenFrameBufferParams.colorFormat = VK_FORMAT_R8G8B8A8_SRGB;
    offscreenFrameBufferParams.depthFormat = VulkanHelper::findDepthFormat(_ctx);
    offscreenFrameBufferParams.colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    offscreenFrameBufferParams.depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    // Create offscreen framebuffers without MSAA here

    // Offscreen Framebuffer (MSAA)
    offscreenFrameBufferParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    offscreenFrameBufferParams.hasResolve = true;
    offscreenFrameBufferParams.msaaSamples = _msaaSamples;
    offscreenFrameBufferParams.resolveFormat = VK_FORMAT_R8G8B8A8_SRGB;
    offscreenFrameBufferParams.colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    offscreenFrameBufferParams.depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    offscreenFrameBufferParams.resolveUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

    _offscreenFrameBuffers[0] = std::make_unique<FrameBuffer>(_ctx, offscreenFrameBufferParams); // Glow pass framebuffer
    _offscreenFrameBuffers[1] = std::make_unique<FrameBuffer>(_ctx, offscreenFrameBufferParams); // Vertical blur framebuffer
    _offscreenFrameBuffers[2] = std::make_unique<FrameBuffer>(_ctx, offscreenFrameBufferParams); // Horizontal blur framebuffer
    _offscreenFrameBuffers[3] = std::make_unique<FrameBuffer>(_ctx, offscreenFrameBufferParams); // Normal rendering framebuffer

    // Main Framebuffers
    FrameBufferParams frameBufferParams{};
    frameBufferParams.extent = _swapChain->getSwapChainExtent();
    frameBufferParams.renderPass = _renderPass->getRenderPass();
    frameBufferParams.msaaSamples = _msaaSamples;
    frameBufferParams.hasColor = true;
    frameBufferParams.hasDepth = true;
    frameBufferParams.hasResolve = true;
    frameBufferParams.colorFormat = _swapChain->getSwapChainImageFormat();
    frameBufferParams.depthFormat = VulkanHelper::findDepthFormat(_ctx);
    frameBufferParams.colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    frameBufferParams.depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    _mainFrameBuffers.resize(_swapChain->getSwapChainImageViews().size());
    for (size_t i = 0; i < _swapChain->getSwapChainImageViews().size(); i++) {
        frameBufferParams.resolveImageView = _swapChain->getSwapChainImageViews()[i];
        _mainFrameBuffers[i] = std::make_unique<FrameBuffer>(_ctx, frameBufferParams);
    }

    // Object Selection Framebuffer
    FrameBufferParams objectSelectionFrameBufferParams{};
    objectSelectionFrameBufferParams.extent = _swapChain->getSwapChainExtent();
    objectSelectionFrameBufferParams.renderPass = _objectSelectionRenderPass->getRenderPass();
    objectSelectionFrameBufferParams.msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    objectSelectionFrameBufferParams.hasColor = true;
    objectSelectionFrameBufferParams.hasDepth = true;
    objectSelectionFrameBufferParams.hasResolve = false;
    objectSelectionFrameBufferParams.colorFormat = VK_FORMAT_R32_UINT;
    objectSelectionFrameBufferParams.depthFormat = VulkanHelper::findDepthFormat(_ctx);
    objectSelectionFrameBufferParams.colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    objectSelectionFrameBufferParams.depthUsage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    _objectSelectionFrameBuffer = std::make_unique<FrameBuffer>(_ctx, objectSelectionFrameBufferParams);
}


void SolarSystemRenderer::buildUI()
{
    // HUD — borderless, pinned top-center
    ImVec2 hudPos(ImGui::GetIO().DisplaySize.x * 0.5f, 12.0f);
    ImGui::SetNextWindowPos(hudPos, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    static bool pauseHovered = false, rewindHovered = false, fwdHovered = false, cogHovered = false;
    const ImVec4 colDefault(1, 1, 1, 0.45f);
    const ImVec4 colHot    (1, 1, 1, 1.00f);

    // Unified speed table: negative = reverse, positive = forward
    static const std::pair<const char*, float> speeds[] = {
        {"-100x",-100.0f},
        {"-20x", -20.0f},
        {"-10x", -10.0f},
        {"-5x",  -5.0f},
        {"-1x",  -1.0f},
        {"1x",    1.0f},
        {"5x",    5.0f},
        {"10x",   10.0f},
        {"20x",   20.0f},
        {"100x",  100.0f},
    };
    static constexpr int speedCount = 10;
    static int speedIndex = 5; // default: 1x

    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
    ImGui::Begin("##hud", nullptr,
        ImGuiWindowFlags_NoDecoration      |
        ImGuiWindowFlags_NoMove            |
        ImGuiWindowFlags_AlwaysAutoResize  |
        ImGuiWindowFlags_NoSavedSettings   |
        ImGuiWindowFlags_NoFocusOnAppearing|
        ImGuiWindowFlags_NoNav);

    ImGui::BeginDisabled(true);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, colDefault);
    ImGui::Button(speeds[speedIndex].first);
    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, rewindHovered ? colHot : colDefault);
    if (ImGui::Button(ICON_FA_FAST_BACKWARD)) {
        speedIndex = std::max(speedIndex - 1, 0);
        _timeScale = speeds[speedIndex].second;
    }
    rewindHovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, pauseHovered ? colHot : colDefault);
    if (ImGui::Button(_isPaused ? ICON_FA_PLAY : ICON_FA_PAUSE))
        _isPaused = !_isPaused;
    pauseHovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, fwdHovered ? colHot : colDefault);
    if (ImGui::Button(ICON_FA_FAST_FORWARD)) {
        speedIndex = std::min(speedIndex + 1, speedCount - 1);
        _timeScale = speeds[speedIndex].second;
    }
    fwdHovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, cogHovered ? colHot : colDefault);
    if (ImGui::Button(ICON_FA_COG))
        _showUI = !_showUI;
    cogHovered = ImGui::IsItemHovered();
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor(4);

    if (_showUI) {
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(280, 0), ImGuiCond_Once);
        ImGui::Begin("Options", &_showUI, ImGuiWindowFlags_NoCollapse);

        ImGui::Text(ICON_FA_TACHOMETER_ALT " %.1f FPS  (%.2f ms)", ImGui::GetIO().Framerate, 1000.0f / ImGui::GetIO().Framerate);

        ImGui::Separator();

        ImGui::Text(ICON_FA_FILM " Scene");
        ImGui::Indent(16.0f);

        if (ImGui::Button(ICON_FA_REDO " Reset"))
            _sceneInfo.time = 0.0f;

        ImGui::Unindent(16.0f);

        ImGui::Separator();

        // Camera
        ImGui::Text(ICON_FA_CAMERA " Camera");
        ImGui::Indent(16.0f);
            glm::vec3 camPos = _camera->getPosition();
            ImGui::Text("Position  %.1f  %.1f  %.1f", camPos.x, camPos.y, camPos.z);
            float radius = _camera->getRadius();
            if (ImGui::SliderFloat("Distance", &radius, 5.0f, 500.0f))
                _camera->setRadius(radius);
        ImGui::Unindent(16.0f);

        ImGui::Separator();

        // Bloom / Blur
        ImGui::Text(ICON_FA_SUN " Bloom");
        ImGui::Indent(16.0f);
            bool changed = false;
            changed |= ImGui::SliderFloat("Scale",    &blurSettings.blurScale,    0.1f, 10.0f);
            changed |= ImGui::SliderFloat("Strength", &blurSettings.blurStrength, 0.0f,  5.0f);
            if (changed)
                _blurSettingsUBO->copyData(&blurSettings, sizeof(blurSettings));
        ImGui::Unindent(16.0f);

        ImGui::End();
    }
}


void SolarSystemRenderer::update(uint32_t currentImage)
{
    Renderer::update(currentImage);

    // Advance time
    auto elapsedTime = std::chrono::high_resolution_clock::now() - _lastFrameTime;
    float elapsedSeconds = std::chrono::duration<float, std::chrono::seconds::period>(elapsedTime).count();
    if(!_isPaused) {
        _sceneInfo.time += elapsedSeconds * _timeScale;
    }
    _lastFrameTime = std::chrono::high_resolution_clock::now();

    VkExtent2D swapChainExtent = _swapChain->getSwapChainExtent();

    // Propagate transforms top-down through scene graph
    _sun->propagate(_sceneInfo.time * 500.f);

    // Update camera position based on time
    _camera->setTarget(_selectableObjects[_currentTargetObjectID]->getPosition());
    // note: camera animations are not affected by pause state
    _camera->advanceAnimation(elapsedSeconds);

    // Update SceneInfo
    _sceneInfo.view = _camera->getViewMatrix();
    _sceneInfo.projection = glm::perspective(glm::radians(45.f), (float)swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 4000.f);
    _sceneInfo.projection[1][1] *= -1; // Invert Y axis for Vulkan
    _sceneInfo.cameraPosition = _camera->getPosition();

    // Update the scene information UBO
    _sceneInfoUBOs[_currentFrame]->copyData(&_sceneInfo, sizeof(_sceneInfo));
}


void SolarSystemRenderer::recordToCommandBuffer(VkCommandBuffer commandBuffer, uint32_t targetSwapImageIndex)
{
    // Command buffer recording started by FramePresenter

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } }; // Clear color
    clearValues[1].depthStencil = { 1.0f, 0 };             // Clear depth value

    // Solar System Scene uses bloom effect

    /*
        First pass (Glow): Render glowing objects to offscreen framebuffer
    */
    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    renderPassBeginInfo.framebuffer = _offscreenFrameBuffers[0]->getFrameBuffer();
    renderPassBeginInfo.renderArea.offset = {0, 0};
    renderPassBeginInfo.renderArea.extent = _offscreenFrameBuffers[0]->getExtent();
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassBeginInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport offscreenViewport{};
    offscreenViewport.x = 0.0f;
    offscreenViewport.y = 0.0f;
    offscreenViewport.width = _offscreenFrameBuffers[0]->getExtent().width;
    offscreenViewport.height = _offscreenFrameBuffers[0]->getExtent().height;
    offscreenViewport.minDepth = 0.0f;
    offscreenViewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &offscreenViewport);

    VkRect2D offscreenScissor{};
    offscreenScissor.offset = {0, 0};
    offscreenScissor.extent = _offscreenFrameBuffers[0]->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &offscreenScissor);


    // Draw sun
    _sun->draw(commandBuffer, *this);

    // Draw sun glow sphere
    _sunGlowSphere->draw(commandBuffer, *this);

    // Draw sun and planets
    // {
    //     VkBuffer vertexBuffers[] = {_sun->getDeviceMesh()->getVertexBuffer()};
    //     VkDeviceSize offsets[] = {0};
    //     vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets); // We can have multiple vertex buffers
    //     vkCmdBindIndexBuffer(commandBuffer, _sun->getDeviceMesh()->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32); // We can only use one index buffer at a time

    //     std::array<VkDescriptorSet, 1> descriptorSets = {
    //         _sceneDescriptorSets[_currentFrame]->getDescriptorSet()  // Per-frame descriptor set
    //     };
    //     vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _glowPipeline->getPipelineLayout(), 0, 1, descriptorSets.data(), 0, nullptr);

    //     // Push constants for model
    //     GlowPassPushConstants pushConstants{};
    //     pushConstants.model = _sun->getModelMatrix();
    //     pushConstants.glowColor = _sun->glowColor;
    //     vkCmdPushConstants(commandBuffer, _glowPipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GlowPassPushConstants), &pushConstants);

    //     vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(_sun->getDeviceMesh()->getIndicesCount()), 1, 0, 0, 0); 
    // }

    // Bind glow pipeline
    _glowPipeline->bind(commandBuffer);
    
    for(int m=0; m<static_cast<int>(_planets.size()); m++) {
        VkBuffer vertexBuffers[] = {_planets[m]->getDeviceMesh()->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets); // We can have multiple vertex buffers
        vkCmdBindIndexBuffer(commandBuffer, _planets[m]->getDeviceMesh()->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32); // We can only use one index buffer at a time

        std::array<VkDescriptorSet, 1> descriptorSets = {
            _sceneDescriptorSets[_currentFrame]->getDescriptorSet()  // Per-frame descriptor set
        };
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _glowPipeline->getPipelineLayout(), 0, 1, descriptorSets.data(), 0, nullptr);

        // Push constants for model
        GlowPassPushConstants pushConstants{};
        pushConstants.model = _planets[m]->getModelMatrix();
        pushConstants.glowColor = glm::vec3(0.f);
        vkCmdPushConstants(commandBuffer, _glowPipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GlowPassPushConstants), &pushConstants);

        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(_planets[m]->getDeviceMesh()->getIndicesCount()), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(commandBuffer);

    /*
        Second pass (Vertical Blur): Apply vertical blur to the glow pass output
    */
    renderPassBeginInfo.framebuffer = _offscreenFrameBuffers[1]->getFrameBuffer();
    renderPassBeginInfo.renderArea.extent = _offscreenFrameBuffers[1]->getExtent();
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    offscreenViewport.width = _offscreenFrameBuffers[1]->getExtent().width;
    offscreenViewport.height = _offscreenFrameBuffers[1]->getExtent().height;
    vkCmdSetViewport(commandBuffer, 0, 1, &offscreenViewport);

    offscreenScissor.extent = _offscreenFrameBuffers[1]->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &offscreenScissor);

    _blurVertPipeline->bind(commandBuffer);
    std::array<VkDescriptorSet, 1> blurVertDescriptorSets = {
        _blurVertDescriptorSet->getDescriptorSet()                 // Blur descriptor set
    };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _blurVertPipeline->getPipelineLayout(), 0, 1, blurVertDescriptorSets.data(), 0, nullptr);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0); // Draw a full-screen triangle for the blur pass
    vkCmdEndRenderPass(commandBuffer);

    
    /*
        Third pass (Horizontal Blur): Apply horizontal blur to the vertical blur output
    */
    renderPassBeginInfo.framebuffer = _offscreenFrameBuffers[2]->getFrameBuffer();
    renderPassBeginInfo.renderArea.extent = _offscreenFrameBuffers[2]->getExtent();
    vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    offscreenViewport.width = _offscreenFrameBuffers[2]->getExtent().width;
    offscreenViewport.height = _offscreenFrameBuffers[2]->getExtent().height;
    vkCmdSetViewport(commandBuffer, 0, 1, &offscreenViewport);

    offscreenScissor.extent = _offscreenFrameBuffers[2]->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &offscreenScissor);

    _blurHorizPipeline->bind(commandBuffer);
    std::array<VkDescriptorSet, 1> blurHorizDescriptorSets = {
        _blurHorizDescriptorSet->getDescriptorSet()                 // Blur descriptor set
    };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _blurHorizPipeline->getPipelineLayout(), 0, 1, blurHorizDescriptorSets.data(), 0, nullptr);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0); // Draw a full-screen triangle for the blur pass
    vkCmdEndRenderPass(commandBuffer);


    /*
        Fourth pass (Normal Shading)
    */
    VkRenderPassBeginInfo mainRenderPassBeginInfo{};
    mainRenderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    mainRenderPassBeginInfo.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    mainRenderPassBeginInfo.framebuffer = _offscreenFrameBuffers[3]->getFrameBuffer();
    mainRenderPassBeginInfo.renderArea.offset = { 0, 0 };
    mainRenderPassBeginInfo.renderArea.extent = _offscreenFrameBuffers[3]->getExtent();
    mainRenderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    mainRenderPassBeginInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &mainRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    offscreenViewport.width = _offscreenFrameBuffers[3]->getExtent().width;
    offscreenViewport.height = _offscreenFrameBuffers[3]->getExtent().height;
    vkCmdSetViewport(commandBuffer, 0, 1, &offscreenViewport);

    offscreenScissor.extent = _offscreenFrameBuffers[3]->getExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &offscreenScissor);

    // Draw skybox
    _skyBox->draw(commandBuffer, *this);

    // Draw sun
    _sun->draw(commandBuffer, *this);

    // Draw planets
    for (const auto& planet : _planets) {
        planet->draw(commandBuffer, *this);
    }

    // // Draw Glow spheres
    for (const auto& glowSphere : _glowSpheres) {
        glowSphere->draw(commandBuffer, *this);
    }

    // Draw orbits
    for (const auto& orbit : _orbits) {
        orbit->draw(commandBuffer, *this);
    }

    vkCmdEndRenderPass(commandBuffer);


    /*
        Fifth pass (Composite): Combine the normal rendering with the glow pass
    */
    VkRenderPassBeginInfo compositeRenderPassBeginInfo{};
    compositeRenderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    compositeRenderPassBeginInfo.renderPass = _renderPass->getRenderPass();
    compositeRenderPassBeginInfo.framebuffer = _mainFrameBuffers[targetSwapImageIndex]->getFrameBuffer();
    compositeRenderPassBeginInfo.renderArea.offset = { 0, 0 };
    compositeRenderPassBeginInfo.renderArea.extent = _swapChain->getSwapChainExtent();
    compositeRenderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    compositeRenderPassBeginInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer, &compositeRenderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) _swapChain->getSwapChainExtent().width;
    viewport.height = (float) _swapChain->getSwapChainExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = _swapChain->getSwapChainExtent();
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    // Bind the composite pipeline
    _compositePipeline->bind(commandBuffer);

    // Bind the descriptor set for the composite pass
    std::array<VkDescriptorSet, 1> compositeDescriptorSets = {
        _compositeDescriptorSet->getDescriptorSet() // Composite descriptor set
    };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _compositePipeline->getPipelineLayout(), 0, 1, compositeDescriptorSets.data(), 0, nullptr);

    // Draw a full-screen quad for the composite pass
    vkCmdDraw(commandBuffer, 3, 1, 0, 0); // Draw a full-screen triangle for the composite pass
    vkCmdEndRenderPass(commandBuffer);

    // Command buffer recording continues in FramePresenter (GUI is appended before end)

}


uint32_t SolarSystemRenderer::querySelectionImage(float mouseX, float mouseY) {
    
    VkCommandBuffer cmdBuffer = VulkanHelper::beginSingleTimeCommands(_ctx);

    VkRenderPassBeginInfo renderPassBeginInfo{};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = _objectSelectionRenderPass->getRenderPass();
    renderPassBeginInfo.framebuffer = _objectSelectionFrameBuffer->getFrameBuffer();
    renderPassBeginInfo.renderArea.offset = { 0, 0 };
    renderPassBeginInfo.renderArea.extent = _objectSelectionFrameBuffer->getExtent();

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color.uint32[0] = 0;                    // Clear color (ID attachment)
    clearValues[1].depthStencil = { 1.0f, 0 };             // Clear depth value
    renderPassBeginInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassBeginInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = (float) _objectSelectionFrameBuffer->getExtent().width;
    viewport.height = (float) _objectSelectionFrameBuffer->getExtent().height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

    //In Object Selection we only care about the pixel under the mouse position, set scissor rect to a 1x1 pixel under mouse
    VkRect2D scissor{};
    scissor.offset = {static_cast<int32_t>(mouseX), static_cast<int32_t>(mouseY)}; // Set scissor rect to mouse position
    scissor.extent = {1, 1}; // 1x1 pixel
    vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);


    // TODO: Should we use SelectableModel's ::drawSelection() function instead of this?

    _objectSelectionPipeline->bind(cmdBuffer);

    // Iterate selectable objects
    for (const auto& pair : _selectableObjects) {
        VkBuffer vertexBuffers[] = {pair.second->getDeviceMesh()->getVertexBuffer()};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cmdBuffer, 0, 1, vertexBuffers, offsets); // We can have multiple vertex buffers
        vkCmdBindIndexBuffer(cmdBuffer, pair.second->getDeviceMesh()->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32); // We can only use one index buffer at a time

        std::array<VkDescriptorSet, 1> descriptorSets = {
            _sceneDescriptorSets[_currentFrame]->getDescriptorSet()  // Per-frame descriptor set
            // Per-model descriptor set is not needed here beacuse we dont care about material when drawing ids.
        };
        vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, _objectSelectionPipeline->getPipelineLayout(), 0, 1, descriptorSets.data(), 0, nullptr);

        // Push constants for model
        ObjectSelectionPushConstants pushConstants {pair.second->getModelMatrix(), static_cast<uint32_t>(pair.second->getID())};
        vkCmdPushConstants(cmdBuffer, _objectSelectionPipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectSelectionPushConstants), &pushConstants);

        vkCmdDrawIndexed(cmdBuffer, static_cast<uint32_t>(pair.second->getDeviceMesh()->getIndicesCount()), 1, 0, 0, 0);
    }

    vkCmdEndRenderPass(cmdBuffer);

    VulkanHelper::endSingleTimeCommands(_ctx, cmdBuffer);

    // Create a staging buffer to read the pixel data from the object selection image
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VkDeviceSize imageSize = _swapChain->getSwapChainExtent().width * _swapChain->getSwapChainExtent().height * sizeof(uint32_t); // Assuming 1 bytes per pixel (uint32_t)
    VulkanHelper::createBuffer(_ctx, imageSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        false, stagingBuffer, stagingBufferMemory);

    // Copy the object selection image to the staging buffer for reading pixel data
    VulkanHelper::copyImageToBuffer(_ctx, _objectSelectionFrameBuffer->getColorImage(), stagingBuffer, _swapChain->getSwapChainExtent().width, _swapChain->getSwapChainExtent().height);

    // Map the memory and read the pixel data
    uint32_t* pixelData = new uint32_t[_swapChain->getSwapChainExtent().width * _swapChain->getSwapChainExtent().height];
    void* data;
    vkMapMemory(_ctx->device, stagingBufferMemory, 0, imageSize, 0, &data);
    memcpy(pixelData, data, (size_t)imageSize);

    // Unmap and free the staging buffer
    vkUnmapMemory(_ctx->device, stagingBufferMemory);
    vkDestroyBuffer(_ctx->device, stagingBuffer, nullptr);
    vkFreeMemory(_ctx->device, stagingBufferMemory, nullptr);

    // Read mouseX and mouseY pixel data
    int mousePixel = static_cast<int>(mouseX) + static_cast<int>(mouseY) * _swapChain->getSwapChainExtent().width;
    uint32_t selectedObjectID = pixelData[mousePixel]; // Assuming the ID is stored in the first channel
    // spdlog::info("Selected object ID: {}", selectedObjectID);

    // uint8_t* pixelData8 = new uint8_t[_swapChain->getSwapChainExtent().width * _swapChain->getSwapChainExtent().height];
    // for (size_t i = 0; i < _swapChain->getSwapChainExtent().width * _swapChain->getSwapChainExtent().height; ++i) {
    //     pixelData8[i] = static_cast<uint8_t>(pixelData[i]); // Convert to uint8_t if needed
    // }

    // // Save the pixel data to a file or process it as needed
    // // For example, you can save it to a PNG file using a library like stb_image_write
    // stbi_write_png("object_selection.png", _swapChain->getSwapChainExtent().width, _swapChain->getSwapChainExtent().height, 1, pixelData8, _swapChain->getSwapChainExtent().width);

    // // Clean up
    // delete[] pixelData8;

    delete[] pixelData;
    return selectedObjectID; // Return the selected object ID
}


void SolarSystemRenderer::handleMouseClick(float mouseX, float mouseY)
{
    // Call the querySelectionImage function to get the selected object ID
    uint32_t objectID = querySelectionImage(mouseX, mouseY);
    if (_currentTargetObjectID == objectID) return;

    // Check if _selectableObjects contains the objectID
    if (_selectableObjects.find(objectID) != _selectableObjects.end()) {
        // key exists
        _currentTargetObjectID = objectID; // Update the current target object ID
        _camera->setTargetAnimated(_selectableObjects[objectID]->getPosition()); // Set the camera target to the selected object
    }
}


void SolarSystemRenderer::handleMouseDrag(float dx, float dy)
{
    // Update camera based on mouse movement
    _camera->rotateHorizontally(static_cast<float>(dx) * 0.005f);
    _camera->rotateVertically(static_cast<float>(dy) * 0.005f);
}


void SolarSystemRenderer::handleMouseWheel(float dy)
{
    // Handle mouse wheel events here if needed
    // For example, you can update the camera zoom level
    float zoomDelta = dy * _camera->getRadius() * 0.03f; // Adjust the zoom speed as needed
    _camera->changeZoom(zoomDelta);
}


void SolarSystemRenderer::handleKeyDown(int key, int scancode, int modifiers)
{
    switch(key) {
        case SDLK_SPACE:
            // Toggle pause
            _isPaused = !_isPaused;
            spdlog::info("Simulation {}", _isPaused ? "paused" : "resumed");
            break;
        case SDLK_C:
            _showUI = !_showUI;
            break;
        case SDLK_0:
            // Reset time to zero
            _sceneInfo.time = 0.f;
            spdlog::info("Simulation time reset to zero");
            break;
        case SDLK_S:
            // Save scene state to file
            saveSceneState("solar_system_scene_state.txt");
            spdlog::info("Scene state saved to solar_system_scene_state.txt");
            break;
        case SDLK_L:
            // Load scene state from file
            loadSceneState("solar_system_scene_state.txt");
            spdlog::info("Scene state loaded from solar_system_scene_state.txt");
            break;
        default:
            break;
    }
}


void SolarSystemRenderer::saveSceneState(const std::string& filename)
{
    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
        spdlog::error("Failed to open file for saving scene state: {}", filename);
        return;
    }

    // Save scene time
    outFile << _sceneInfo.time << std::endl;

    // Save current target object ID
    outFile << _currentTargetObjectID << std::endl;

    // Save camera state
    // Up
    outFile << _camera->getUp().x << "," << _camera->getUp().y << "," << _camera->getUp().z << std::endl;
    // Forward
    outFile << _camera->getForward().x << "," << _camera->getForward().y << "," << _camera->getForward().z << std::endl;
    // Left
    outFile << _camera->getLeft().x << "," << _camera->getLeft().y << "," << _camera->getLeft().z << std::endl;
    // Target
    outFile << _camera->getTarget().x << "," << _camera->getTarget().y << "," << _camera->getTarget().z << std::endl;
    // Radius
    outFile << _camera->getRadius() << std::endl;

    outFile.close();
}


void SolarSystemRenderer::loadSceneState(const std::string& filename)
{
    std::ifstream inFile(filename);
    if (!inFile.is_open()) {
        spdlog::error("Failed to open file for loading scene state: {}", filename);
        return;
    }

    std::string line;

    // Load scene time
    std::getline(inFile, line);
    _sceneInfo.time = std::stof(line);

    // Load current target object ID
    std::getline(inFile, line);
    _currentTargetObjectID = std::stoul(line);

    // Load camera state
    // Up
    std::getline(inFile, line);
    glm::vec3 up;
    sscanf(line.c_str(), "%f,%f,%f", &up.x, &up.y, &up.z);
    _camera->setUp(up);
    // Forward
    std::getline(inFile, line);
    glm::vec3 forward;
    sscanf(line.c_str(), "%f,%f,%f", &forward.x, &forward.y, &forward.z);
    _camera->setForward(forward);
    // Left
    std::getline(inFile, line);
    glm::vec3 left;
    sscanf(line.c_str(), "%f,%f,%f", &left.x, &left.y, &left.z);
    _camera->setLeft(left);
    // Target
    std::getline(inFile, line);
    glm::vec3 target;
    sscanf(line.c_str(), "%f,%f,%f", &target.x, &target.y, &target.z);
    _camera->setTarget(target);
    // Radius
    std::getline(inFile, line);
    float radius = std::stof(line);
    _camera->setRadius(radius);


    inFile.close();
}