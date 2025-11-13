#include "SolarSystemScene.h"
#include "geometry/MeshFactory.h"
#include "TextureSampler.h"
#include "TextureCubemap.h"


SolarSystemScene::SolarSystemScene(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<SwapChain> swapChain)
    : Scene(std::move(ctx), std::move(swapChain))
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
        _sceneInfoUBOs[i] = std::make_unique<UniformBuffer<SceneInfo>>(_ctx);
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
    _camera = std::make_unique<Camera>(cameraParams);
}


SolarSystemScene::~SolarSystemScene()
{
    // Wait for any unfinished GPU tasks
    vkDeviceWaitIdle(_ctx->device);

    _planetPipeline = nullptr;
    _orbitPipeline = nullptr;
    _glowSpherePipeline = nullptr;
    _skyBoxPipeline = nullptr;
    _sunPipeline = nullptr;
    _earthPipeline = nullptr;

    _renderPass = nullptr;
    _offscreenRenderPass = nullptr;
    _offscreenRenderPassMSAA = nullptr;
    _objectSelectionRenderPass = nullptr;
}


void SolarSystemScene::createPipelines()
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
    _glowPipeline = std::make_unique<Pipeline>(_ctx, "spv/glow/glow_vert.spv", "spv/glow/glow_frag.spv", glowPassPipelineParams);

    VkDescriptorImageInfo glowPassOutputTexture{};
    glowPassOutputTexture.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    glowPassOutputTexture.imageView = _offscreenFrameBuffers[0]->getResolveImageView();
    glowPassOutputTexture.sampler = _ppTextureSampler->getSampler();


    // Blur pass pipeline (vertical)
    _blurSettingsUBO = std::make_unique<UniformBuffer<BlurSettings>>(_ctx);
    _blurSettingsUBO->update(blurSettings);

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
    _blurVertPipeline = std::make_unique<Pipeline>(_ctx, "spv/blur/blur_vert.spv", "spv/blur/blur_frag.spv", blurPassPipelineParams);


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
    _blurHorizPipeline = std::make_unique<Pipeline>(_ctx, "spv/blur/blur_vert.spv", "spv/blur/blur_frag.spv", blurPassPipelineParams);


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
    _compositePipeline = std::make_unique<Pipeline>(_ctx, "spv/composite/composite_vert.spv", "spv/composite/composite_frag.spv", compositePipelineParams);

    
    // Planet pipeline
    PipelineParams planetPipelineParams;
    planetPipelineParams.name = "PlanetPipeline";
    planetPipelineParams.descriptorSetLayouts = {sceneDSL, _planets[0]->getDescriptorSet()->getDescriptorSetLayout()};
    planetPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    planetPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    planetPipelineParams.msaaSamples = _msaaSamples;
    _planetPipeline = std::make_unique<Pipeline>(_ctx, "spv/planet/planet_vert.spv", "spv/planet/planet_frag.spv", planetPipelineParams);

    // Orbit pipeline
    PipelineParams orbitPipelineParams;
    orbitPipelineParams.name = "OrbitPipeline";
    orbitPipelineParams.descriptorSetLayouts = {sceneDSL};
    orbitPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)}};
    orbitPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    orbitPipelineParams.msaaSamples = _msaaSamples;
    orbitPipelineParams.depthTest = true;
    orbitPipelineParams.depthWrite = false;
    _orbitPipeline = std::make_unique<Pipeline>(_ctx, "spv/orbit/orbit_vert.spv", "spv/orbit/orbit_frag.spv", orbitPipelineParams);

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
    _glowSpherePipeline = std::make_unique<Pipeline>(_ctx, "spv/glowsphere/glowsphere_vert.spv", "spv/glowsphere/glowsphere_frag.spv", glowSpherePipelineParams);

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
    _skyBoxPipeline = std::make_unique<Pipeline>(_ctx, "spv/skybox/skybox_vert.spv", "spv/skybox/skybox_frag.spv", skyBoxPipelineParams);

    // Earth pipeline
    PipelineParams earthPipelineParams;
    earthPipelineParams.name = "EarthPipeline";
    earthPipelineParams.descriptorSetLayouts = {sceneDSL, _earth->getDescriptorSet()->getDescriptorSetLayout()};
    earthPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    earthPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    earthPipelineParams.msaaSamples = _msaaSamples;
    _earthPipeline = std::make_unique<Pipeline>(_ctx, "spv/earth/earth_vert.spv", "spv/earth/earth_frag.spv", earthPipelineParams);

    // Sun pipeline
    PipelineParams sunPipelineParams;
    sunPipelineParams.name = "SunPipeline";
    sunPipelineParams.descriptorSetLayouts = {sceneDSL};
    sunPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    sunPipelineParams.renderPass = _offscreenRenderPassMSAA->getRenderPass();
    sunPipelineParams.msaaSamples = _msaaSamples;
    _sunPipeline = std::make_unique<Pipeline>(_ctx, "spv/sun/sun_vert.spv", "spv/sun/sun_frag.spv", sunPipelineParams);

    // Object selection pipeline
    PipelineParams objectSelectionPipelineParams;
    objectSelectionPipelineParams.name = "ObjectSelectionPipeline";
    objectSelectionPipelineParams.descriptorSetLayouts = {sceneDSL};
    objectSelectionPipelineParams.pushConstantRanges = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(ObjectSelectionPushConstants)}};
    objectSelectionPipelineParams.renderPass = _objectSelectionRenderPass->getRenderPass();
    objectSelectionPipelineParams.blendEnable = false;
    _objectSelectionPipeline = std::make_unique<Pipeline>(_ctx, "spv/selection/select_vert.spv", "spv/selection/select_frag.spv", objectSelectionPipelineParams);
}


void SolarSystemScene::connectPipelines()
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

    // Earth and sun have their own pipelines
    _sun->setPipeline(_sunPipeline);
    _earth->setPipeline(_earthPipeline);
}


void SolarSystemScene::createModels()
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
    std::shared_ptr<TextureCubemap> skyTexture = std::make_shared<TextureCubemap>(_ctx, "textures/skybox", VK_FORMAT_R8G8B8A8_SRGB);
    _skyBox = std::make_unique<SkyBox>(_ctx, "Skybox", cubeDMesh, skyTexture);

    // Sun
    _sun = std::make_shared<Sun>(_ctx, "Sun", sphereDMesh, sizeSun);
    _selectableObjects[_sun->getID()] = _sun;

    // Mercury
    std::shared_ptr<Texture2D> mercuryColorTexture = std::make_shared<Texture2D>(_ctx, "textures/mercury/8k_mercury.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> mercury = std::make_shared<Planet>(_ctx, "Mercury", sphereDMesh, mercuryColorTexture, _sun, 
        sizeMercury, orbitRadMercury, orbitAtT0Mercury, orbitSpeedMercury, spinAtT0Mercury, spinSpeedMercury);
    _selectableObjects[mercury->getID()] = mercury;
    _planets.push_back(std::move(mercury));

    // Venus
    std::shared_ptr<Texture2D> venusColorTexture = std::make_shared<Texture2D>(_ctx, "textures/venus/4k_venus_atmosphere.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> venus = std::make_shared<Planet>(_ctx, "Venus", sphereDMesh, venusColorTexture, _sun, 
        sizeVenus, orbitRadVenus, orbitAtT0Venus, orbitSpeedVenus, spinAtT0Venus, spinSpeedVenus);
    _glowSpheres.push_back(std::make_unique<GlowSphere>(_ctx, "VenusGlow", sphereDMesh, venus, glm::vec4(0.74f, 0.69f, 0.2f, 1.f), 3.f, 4.f, sizeVenus * 1.03f, false));
    _selectableObjects[venus->getID()] = venus;
    _planets.push_back(std::move(venus));

    // Earth
    std::shared_ptr<Texture2D> colorTexture = std::make_shared<Texture2D>(_ctx, "textures/earth/10k_earth_day.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Texture2D> unlitTexture = std::make_shared<Texture2D>(_ctx, "textures/earth/10k_earth_night.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Texture2D> normalTexture = std::make_shared<Texture2D>(_ctx, "textures/earth/2k_earth_normal.png", VK_FORMAT_R8G8B8A8_UNORM);
    std::shared_ptr<Texture2D> specularTexture = std::make_shared<Texture2D>(_ctx, "textures/earth/2k_earth_specular.jpeg", VK_FORMAT_R8G8B8A8_UNORM);
    std::shared_ptr<Texture2D> overlayTexture = std::make_shared<Texture2D>(_ctx, "textures/earth/8k_earth_clouds.png", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Earth> earth = std::make_shared<Earth>(_ctx, "Earth", sphereDMesh, colorTexture, unlitTexture, normalTexture, specularTexture, overlayTexture, _sun,
         sizeEarth, orbitRadEarth, orbitAtT0Earth, orbitSpeedEarth, spinAtT0Earth, spinSpeedEarth);
    _earth = earth;
    _glowSpheres.push_back(std::make_unique<GlowSphere>(_ctx, "EarthGlow", sphereDMesh, _earth, glm::vec4(0.45f, 0.55f, 1.f, 1.f), 3.f, 4.f, sizeEarth * 1.03f, false));
    _selectableObjects[earth->getID()] = earth;
    _planets.push_back(std::move(earth));

    // Earth Moon
    std::shared_ptr<Texture2D> moonColorTexture = std::make_shared<Texture2D>(_ctx, "textures/moon/8k_moon.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> moon = std::make_shared<Planet>(_ctx, "Moon", sphereDMesh, moonColorTexture, _earth, 
        sizeMoon, orbitRadMoon, orbitAtT0Moon, orbitSpeedMoon, spinAtT0Moon, spinSpeedMoon);
    _selectableObjects[moon->getID()] = moon;
    _planets.push_back(std::move(moon));

    // Mars
    std::shared_ptr<Texture2D> marsColorTexture = std::make_shared<Texture2D>(_ctx, "textures/mars/8k_mars.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> mars = std::make_shared<Planet>(_ctx, "Mars", sphereDMesh, marsColorTexture, _sun,
        sizeMars, orbitRadMars, orbitAtT0Mars, orbitSpeedMars, spinAtT0Mars, spinSpeedMars);
    _selectableObjects[mars->getID()] = mars;
    _planets.push_back(std::move(mars));

    // Jupiter
    std::shared_ptr<Texture2D> jupiterColorTexture = std::make_shared<Texture2D>(_ctx, "textures/jupiter/4k_jupiter.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> jupiter = std::make_shared<Planet>(_ctx, "Jupiter", sphereDMesh, jupiterColorTexture, _sun, 
        sizeJupiter, orbitRadJupiter, orbitAtT0Jupiter, orbitSpeedJupiter, spinAtT0Jupiter, spinSpeedJupiter);
    _selectableObjects[jupiter->getID()] = jupiter;
    _planets.push_back(std::move(jupiter));

    // Saturn
    std::shared_ptr<Texture2D> saturnColorTexture = std::make_shared<Texture2D>(_ctx, "textures/saturn/8k_saturn.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> saturn = std::make_shared<Planet>(_ctx, "Saturn", sphereDMesh, saturnColorTexture, _sun,
        sizeSaturn, orbitRadSaturn, orbitAtT0Saturn, orbitSpeedSaturn, spinAtT0Saturn, spinSpeedSaturn);
    _selectableObjects[saturn->getID()] = saturn;
    _planets.push_back(std::move(saturn));

    // Saturn Ring
    std::shared_ptr<Texture2D> ringTexture = std::make_shared<Texture2D>(_ctx, "textures/saturn/8k_saturn_ring_alpha.png", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> saturn_ring = std::make_shared<Planet>(_ctx, "SaturnRing", ringDMesh, ringTexture, _sun, 
        sizeSaturnRing, orbitRadSaturn, orbitAtT0Saturn, orbitSpeedSaturn, spinAtT0Saturn, spinSpeedSaturn);
    _planets.push_back(std::move(saturn_ring));

    // Uranus
    std::shared_ptr<Texture2D> uranusColorTexture = std::make_shared<Texture2D>(_ctx, "textures/uranus/1k_uranus.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> uranus = std::make_shared<Planet>(_ctx, "Uranus", sphereDMesh, uranusColorTexture, _sun,
        sizeUranus, orbitRadUranus, orbitAtT0Uranus, orbitSpeedUranus, spinAtT0Uranus, spinSpeedUranus);
    _selectableObjects[uranus->getID()] = uranus;
    _planets.push_back(std::move(uranus));

    // Neptune
    std::shared_ptr<Texture2D> neptuneColorTexture = std::make_shared<Texture2D>(_ctx, "textures/neptune/2k_neptune.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> neptune = std::make_shared<Planet>(_ctx, "Neptune", sphereDMesh, neptuneColorTexture, _sun, 
        sizeNeptune, orbitRadNeptune, orbitAtT0Neptune, orbitSpeedNeptune, spinAtT0Neptune, spinSpeedNeptune);
    _selectableObjects[neptune->getID()] = neptune;
    _planets.push_back(std::move(neptune));

    // Pluto
    std::shared_ptr<Texture2D> plutoColorTexture = std::make_shared<Texture2D>(_ctx, "textures/pluto/2k_pluto.jpg", VK_FORMAT_R8G8B8A8_SRGB);
    std::shared_ptr<Planet> pluto = std::make_shared<Planet>(_ctx, "Pluto", sphereDMesh, plutoColorTexture, _sun,
        sizePluto, orbitRadPluto, orbitAtT0Pluto, orbitSpeedPluto, spinAtT0Pluto, spinSpeedPluto);
    _selectableObjects[pluto->getID()] = pluto;
    _planets.push_back(std::move(pluto));


    // Mercury Orbit
    _orbits.push_back(std::make_unique<Orbit>(_ctx, "MercuryOrbit", quadDMesh, _sun, orbitRadMercury, orbitAtT0Mercury, orbitSpeedMercury));

    // Venus Orbit
    _orbits.push_back(std::make_unique<Orbit>(_ctx, "VenusOrbit", quadDMesh, _sun, orbitRadVenus, orbitAtT0Venus, orbitSpeedVenus));

    // Earth Orbit
    _orbits.push_back(std::make_unique<Orbit>(_ctx, "EarthOrbit", quadDMesh, _sun, orbitRadEarth, orbitAtT0Earth, orbitSpeedEarth));

    // Moon Orbit
    _orbits.push_back(std::make_unique<Orbit>(_ctx, "MoonOrbit", quadDMesh, _earth, orbitRadMoon, orbitAtT0Moon, orbitSpeedMoon));

    // Mars Orbit
    _orbits.push_back(std::make_unique<Orbit>(_ctx, "MarsOrbit", quadDMesh, _sun, orbitRadMars, orbitAtT0Mars, orbitSpeedMars));

    // Jupiter Orbit
    _orbits.push_back(std::make_unique<Orbit>(_ctx, "JupiterOrbit", quadDMesh, _sun, orbitRadJupiter, orbitAtT0Jupiter, orbitSpeedJupiter));

    // Saturn Orbit
    _orbits.push_back(std::make_unique<Orbit>(_ctx, "SaturnOrbit", quadDMesh, _sun, orbitRadSaturn, orbitAtT0Saturn, orbitSpeedSaturn));

    // Uranus Orbit
    _orbits.push_back(std::make_unique<Orbit>(_ctx, "UranusOrbit", quadDMesh, _sun, orbitRadUranus, orbitAtT0Uranus, orbitSpeedUranus));

    // Neptune Orbit
    _orbits.push_back(std::make_unique<Orbit>(_ctx, "NeptuneOrbit", quadDMesh, _sun, orbitRadNeptune, orbitAtT0Neptune, orbitSpeedNeptune));

    // Pluto Orbit
    _orbits.push_back(std::make_unique<Orbit>(_ctx, "PlutoOrbit", quadDMesh, _sun, orbitRadPluto, orbitAtT0Pluto, orbitSpeedPluto));


    // Glow spheres
    _sunGlowSphere = std::make_unique<GlowSphere>(_ctx, "SunGlow", sphereDMesh, _sun, glm::vec4(1.f, 0.4f, 0.0f, 0.4f), 0.5f, 3.0f, sizeSun * 2.f, true);
    //TODO: need to expose these parameters in the UI
}


void SolarSystemScene::createRenderPasses() {
    
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
    screenRenderPassParams.resolveAttachmentLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
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


void SolarSystemScene::createFrameBuffers() {
    
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


void SolarSystemScene::update(uint32_t currentImage)
{
    Scene::update(currentImage);

    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();

    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    VkExtent2D swapChainExtent = _swapChain->getSwapChainExtent();

    // Update the planet positions
    _sun->calculateModelMatrix();
    for (const auto& planet : _planets) {
        planet->calculateModelMatrix(time * 4000.f);
    }
    for (const auto& orbit : _orbits) {
        orbit->calculateModelMatrix(time * 4000.f);
    }
    _sunGlowSphere->calculateModelMatrix();
    for (const auto& glowSphere : _glowSpheres) {
        glowSphere->calculateModelMatrix();
    }

    // Update camera position based on time
    _camera->setTarget(_selectableObjects[_currentTargetObjectID]->getPosition());
    _camera->advanceAnimation(time - _sceneInfo.time);

    // Update SceneInfo
    _sceneInfo.view = _camera->getViewMatrix();
    _sceneInfo.projection = glm::perspective(glm::radians(45.f), (float)swapChainExtent.width / (float)swapChainExtent.height, 0.1f, 4000.f);
    _sceneInfo.projection[1][1] *= -1; // Invert Y axis for Vulkan
    _sceneInfo.time = time;
    _sceneInfo.cameraPosition = _camera->getPosition();

    // Update the scene information UBO
    _sceneInfoUBOs[_currentFrame]->update(_sceneInfo);
}


void SolarSystemScene::recordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t targetSwapImageIndex)
{
    // Begin Command buffer recording
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = 0;
    beginInfo.pInheritanceInfo = nullptr;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        spdlog::error("Failed to begin recording command buffer!");
        return;
    }

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

    // End the command buffer recording
    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        spdlog::error("Failed to record command buffer!");
        return;
    }

}


uint32_t SolarSystemScene::querySelectionImage(float mouseX, float mouseY) {
    
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
        stagingBuffer, stagingBufferMemory);

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


void SolarSystemScene::handleMouseClick(float mouseX, float mouseY)
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


void SolarSystemScene::handleMouseDrag(float dx, float dy)
{
    // Update camera based on mouse movement
    _camera->rotateHorizontally(static_cast<float>(dx) * 0.005f);
    _camera->rotateVertically(static_cast<float>(dy) * 0.005f);
}


void SolarSystemScene::handleMouseWheel(float dy)
{
    // Handle mouse wheel events here if needed
    // For example, you can update the camera zoom level
    float zoomDelta = dy * _camera->getRadius() * 0.03f; // Adjust the zoom speed as needed
    _camera->changeZoom(zoomDelta);
}