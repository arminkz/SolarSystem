#include "SolarSystem.h"

#include "vulkan/resources/TextureCubemap.h"
#include "core/AssetPath.h"
#include "geometry/MeshFactory.h"
#include "gui/FontAwesome.h"


SolarSystem::SolarSystem(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<SwapChain> swapChain)
    : MultiPassRenderer(std::move(ctx), std::move(swapChain))
{
    createModels();
    createScenePipelines();
    connectPipelines();

    //TODO: should we bring camera to this class?
    _currentTargetObjectID = _sun->getID();
    CameraParams cameraParams{};
    cameraParams.target = _sun->getPosition();
    cameraParams.radius = glm::length(glm::vec3(0.f, 150.f, 130.f));
    cameraParams.initialForward = glm::normalize(glm::vec3(0.f, -150.f, -130.f));
    _camera = std::make_unique<Camera>(cameraParams);
}


SolarSystem::~SolarSystem()
{
    vkDeviceWaitIdle(_ctx->device);

    _planetPipeline = nullptr;
    _orbitPipeline = nullptr;
    _glowSpherePipeline = nullptr;
    _skyBoxPipeline = nullptr;
    _sunPipeline = nullptr;
    _earthPipeline = nullptr;
    _ringPipeline = nullptr;
}


void SolarSystem::advance()
{
    //TODO: should we move this to MultiPassRenderer class?
    // Advance time 
    auto now = std::chrono::high_resolution_clock::now();
    float elapsedSec = std::chrono::duration<float>(now - _lastFrameTime).count();
    _lastFrameTime = now;

    if (!_isPaused)
        _sceneInfo.time += elapsedSec * _timeScale;

    // Propagate scene graph top-down
    _sun->propagate(_sceneInfo.time * 500.f);

    // Update camera target to currently selected object, then advance animation
    _camera->setTarget(_selectableModels[_currentTargetObjectID]->getPosition());
    _camera->advanceAnimation(elapsedSec);
}


void SolarSystem::buildUI()
{
    // HUD — borderless, pinned top-center
    ImVec2 hudPos(ImGui::GetIO().DisplaySize.x * 0.5f, 12.0f);
    ImGui::SetNextWindowPos(hudPos, ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);
    static bool pauseHovered = false, rewindHovered = false, fwdHovered = false, cogHovered = false;
    const ImVec4 colDefault(1, 1, 1, 0.45f);
    const ImVec4 colHot    (1, 1, 1, 1.00f);

    static const std::pair<const char*, float> speeds[] = {
        {"-100x",  -100.0f},
        {"-20x",    -20.0f},
        {"-10x",    -10.0f},
        {"-5x",      -5.0f},
        {"-1x",      -1.0f},
        {"-0.5x",    -0.5f},
        {"-0.25x",  -0.25f},
        {"0.25x",    0.25f},
        {"0.5x",      0.5f},
        {"1x",        1.0f},
        {"5x",        5.0f},
        {"10x",      10.0f},
        {"20x",      20.0f},
        {"100x",    100.0f},
    };
    static constexpr int speedCount = 14;
    static int speedIndex = 9; // default: 1x

    ImGui::PushStyleColor(ImGuiCol_Border,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
    ImGui::Begin("##hud", nullptr,
        ImGuiWindowFlags_NoDecoration       |
        ImGuiWindowFlags_NoMove             |
        ImGuiWindowFlags_AlwaysAutoResize   |
        ImGuiWindowFlags_NoSavedSettings    |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav);

    ImGui::BeginDisabled(true);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, colDefault);
    ImGui::Button(speeds[speedIndex].first);
    ImGui::PopStyleColor();
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::PushStyleColor(ImGuiCol_Text, rewindHovered ? colHot : colDefault);
    if (ImGui::Button(ICON_FA_FAST_BACKWARD)) {
        speedIndex  = std::max(speedIndex - 1, 0);
        _timeScale  = speeds[speedIndex].second;
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
        speedIndex  = std::min(speedIndex + 1, speedCount - 1);
        _timeScale  = speeds[speedIndex].second;
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

        ImGui::Text(ICON_FA_CAMERA " Camera");
        ImGui::Indent(16.0f);
        glm::vec3 camPos = _camera->getPosition();
        ImGui::Text("Position  %.1f  %.1f  %.1f", camPos.x, camPos.y, camPos.z);
        float radius = _camera->getRadius();
        if (ImGui::SliderFloat("Distance", &radius, 5.0f, 500.0f))
            _camera->setRadius(radius);
        ImGui::Unindent(16.0f);

        ImGui::Separator();

        ImGui::Text(ICON_FA_SUN " Bloom");
        ImGui::Indent(16.0f);
        bool changed = false;
        changed |= ImGui::SliderFloat("Scale",    &_bloomScale,    0.1f, 10.0f);
        changed |= ImGui::SliderFloat("Strength", &_bloomStrength, 0.0f,  5.0f);
        if (changed)
            setBloomSettings(_bloomScale, _bloomStrength);
        ImGui::Unindent(16.0f);

        ImGui::End();
    }
}


void SolarSystem::handleKeyDown(int key, int scancode, int modifiers)
{
    // Number-key planet selection: 0=Sun, 1-9=Mercury…Pluto (Moon skipped).
    // Each entry is (objectID, arrivalRadius). IDs follow creation order in createModels().
    static const std::array<std::pair<int, float>, 10> keyToTarget = {{
        {1,  sizeSun     * 8.0f},   // 0 = Sun
        {2,  sizeMercury * 8.0f},   // 1 = Mercury
        {3,  sizeVenus   * 8.0f},   // 2 = Venus
        {4,  sizeEarth   * 8.0f},   // 3 = Earth
        {6,  sizeMars    * 8.0f},   // 4 = Mars  (Moon = ID 5, skipped)
        {7,  sizeJupiter * 8.0f},   // 5 = Jupiter
        {8,  sizeSaturn  * 8.0f},   // 6 = Saturn
        {9,  sizeUranus  * 8.0f},   // 7 = Uranus
        {10, sizeNeptune * 8.0f},   // 8 = Neptune
        {11, sizePluto   * 8.0f},   // 9 = Pluto
    }};

    if (key >= SDLK_0 && key <= SDLK_9) {
        int slot = (key == SDLK_0) ? 0 : (key - SDLK_0);
        const auto& [id, arrivalRadius] = keyToTarget[slot];
        if (id != static_cast<int>(_currentTargetObjectID)) {
            glm::vec3 newPos = _selectableModels[id]->getPosition();
            float pullout = std::max(_camera->getRadius() * 1.5f, 250.0f);
            _camera->switchTargetAnimated(newPos, pullout, arrivalRadius);
            _currentTargetObjectID = id;
            spdlog::info("Camera target switched to {}", _selectableModels[id]->getName());
        }
        return;
    }

    switch (key) {
        case SDLK_SPACE:
            _isPaused = !_isPaused;
            spdlog::info("Simulation {}", _isPaused ? "paused" : "resumed");
            break;
        case SDLK_C:
            _showUI = !_showUI;
            break;
        case SDLK_R:
            _sceneInfo.time = 0.f;
            spdlog::info("Simulation time reset to zero");
            break;
        case SDLK_S:
            saveSceneState("solar_system_scene_state.txt");
            spdlog::info("Scene state saved to solar_system_scene_state.txt");
            break;
        case SDLK_L:
            loadSceneState("solar_system_scene_state.txt");
            spdlog::info("Scene state loaded from solar_system_scene_state.txt");
            break;
        default:
            break;
    }
}


void SolarSystem::createModels()
{
    HostMesh sphere = MeshFactory::createSphereMesh(1.f, 64, 64);
    HostMesh ring   = MeshFactory::createAnnulusMesh(1.3f, 2.2f, 64);
    HostMesh quad   = MeshFactory::createQuadMesh(1.f, 1.f, true);
    HostMesh cube   = MeshFactory::createCubeMesh(1.f, 1.f, 1.f);

    std::shared_ptr<DeviceMesh> sphereDMesh = std::make_shared<DeviceMesh>(_ctx, sphere);
    std::shared_ptr<DeviceMesh> ringDMesh   = std::make_shared<DeviceMesh>(_ctx, ring);
    std::shared_ptr<DeviceMesh> quadDMesh   = std::make_shared<DeviceMesh>(_ctx, quad);
    std::shared_ptr<DeviceMesh> cubeDMesh   = std::make_shared<DeviceMesh>(_ctx, cube);

    // SkyBox
    auto skyTexture = std::make_shared<TextureCubemap>(_ctx,
        AssetPath::getInstance()->get("textures/skybox"), VK_FORMAT_R8G8B8A8_SRGB);
    _skyBox = std::make_shared<SkyBox>(_ctx, "Skybox", cubeDMesh, skyTexture);

    // Sun
    _sun = std::make_shared<Sun>(_ctx, "Sun", sphereDMesh, sizeSun);
    _selectableModels[_sun->getID()] = _sun;

    // Mercury
    auto mercuryTex = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/mercury/8k_mercury.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    auto mercury = std::make_shared<Planet>(_ctx, "Mercury", sphereDMesh, mercuryTex,
        sizeMercury, orbitRadMercury, orbitOffsetMercury, orbitVelocityMercury,
        spinVelocityMercury, orbitInclinationMercury, axialTiltMercury);
    _selectableModels[mercury->getID()] = mercury;
    _planets.push_back(mercury);

    // Venus
    auto venusTex = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/venus/4k_venus_atmosphere.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    auto venus = std::make_shared<Planet>(_ctx, "Venus", sphereDMesh, venusTex,
        sizeVenus, orbitRadVenus, orbitOffsetVenus, orbitVelocityVenus,
        spinVelocityVenus, orbitInclinationVenus, axialTiltVenus);
    auto venusGlow = std::make_shared<GlowSphere>(_ctx, "VenusGlow", sphereDMesh,
        glm::vec4(0.74f, 0.69f, 0.2f, 1.f), 3.f, 4.f, sizeVenus * 1.03f, false);
    _glowSpheres.push_back(venusGlow);
    _selectableModels[venus->getID()] = venus;
    _planets.push_back(venus);

    // Earth
    auto earthDay     = std::make_shared<Texture2D>(_ctx, AssetPath::getInstance()->get("textures/earth/10k_earth_day.jpg"),     VK_FORMAT_R8G8B8A8_SRGB);
    auto earthNight   = std::make_shared<Texture2D>(_ctx, AssetPath::getInstance()->get("textures/earth/10k_earth_night.jpg"),   VK_FORMAT_R8G8B8A8_SRGB);
    auto earthNormal  = std::make_shared<Texture2D>(_ctx, AssetPath::getInstance()->get("textures/earth/2k_earth_normal.png"),   VK_FORMAT_R8G8B8A8_UNORM);
    auto earthSpecular= std::make_shared<Texture2D>(_ctx, AssetPath::getInstance()->get("textures/earth/2k_earth_specular.jpeg"),VK_FORMAT_R8G8B8A8_UNORM);
    auto earthClouds  = std::make_shared<Texture2D>(_ctx, AssetPath::getInstance()->get("textures/earth/8k_earth_clouds.png"),   VK_FORMAT_R8G8B8A8_SRGB);
    _earth = std::make_shared<Earth>(_ctx, "Earth", sphereDMesh, earthDay, earthNight, earthNormal, earthSpecular, earthClouds,
        sizeEarth, orbitRadEarth, orbitOffsetEarth, orbitVelocityEarth,
        spinVelocityEarth, orbitInclinationEarth, axialTiltEarth);
    auto earthGlow = std::make_shared<GlowSphere>(_ctx, "EarthGlow", sphereDMesh,
        glm::vec4(0.45f, 0.55f, 1.f, 1.f), 3.f, 4.f, sizeEarth * 1.03f, false);
    _glowSpheres.push_back(earthGlow);
    _selectableModels[_earth->getID()] = _earth;
    _planets.push_back(_earth);

    // Moon
    auto moonTex = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/moon/8k_moon.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    auto moon = std::make_shared<Planet>(_ctx, "Moon", sphereDMesh, moonTex,
        sizeMoon, orbitRadMoon, orbitOffsetMoon, orbitVelocityMoon,
        spinVelocityMoon, orbitInclinationMoon, axialTiltMoon);
    _selectableModels[moon->getID()] = moon;
    _planets.push_back(moon);

    // Mars
    auto marsTex = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/mars/8k_mars.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    auto mars = std::make_shared<Planet>(_ctx, "Mars", sphereDMesh, marsTex,
        sizeMars, orbitRadMars, orbitOffsetMars, orbitVelocityMars,
        spinVelocityMars, orbitInclinationMars, axialTiltMars);
    _selectableModels[mars->getID()] = mars;
    _planets.push_back(mars);

    // Jupiter
    auto jupiterTex = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/jupiter/4k_jupiter.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    auto jupiter = std::make_shared<Planet>(_ctx, "Jupiter", sphereDMesh, jupiterTex,
        sizeJupiter, orbitRadJupiter, orbitOffsetJupiter, orbitVelocityJupiter,
        spinVelocityJupiter, orbitInclinationJupiter, axialTiltJupiter);
    _selectableModels[jupiter->getID()] = jupiter;
    _planets.push_back(jupiter);

    // Saturn
    auto saturnTex = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/saturn/8k_saturn.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    auto saturn = std::make_shared<Planet>(_ctx, "Saturn", sphereDMesh, saturnTex,
        sizeSaturn, orbitRadSaturn, orbitOffsetSaturn, orbitVelocitySaturn,
        spinVelocitySaturn, orbitInclinationSaturn, axialTiltSaturn);
    _selectableModels[saturn->getID()] = saturn;
    _planets.push_back(saturn);

    // Saturn Ring
    auto ringTex = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/saturn/8k_saturn_ring_alpha.png"), VK_FORMAT_R8G8B8A8_SRGB);
    auto saturnRing = std::make_shared<Planet>(_ctx, "SaturnRing", ringDMesh, ringTex,
        sizeSaturnRing, orbitRadSaturn, orbitOffsetSaturn, orbitVelocitySaturn,
        0.f, orbitInclinationSaturn, axialTiltSaturn);
    _planets.push_back(saturnRing);

    // Uranus
    auto uranusTex = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/uranus/1k_uranus.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    auto uranus = std::make_shared<Planet>(_ctx, "Uranus", sphereDMesh, uranusTex,
        sizeUranus, orbitRadUranus, orbitOffsetUranus, orbitVelocityUranus,
        spinVelocityUranus, orbitInclinationUranus, axialTiltUranus);
    _selectableModels[uranus->getID()] = uranus;
    _planets.push_back(uranus);

    // Neptune
    auto neptuneTex = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/neptune/2k_neptune.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    auto neptune = std::make_shared<Planet>(_ctx, "Neptune", sphereDMesh, neptuneTex,
        sizeNeptune, orbitRadNeptune, orbitOffsetNeptune, orbitVelocityNeptune,
        spinVelocityNeptune, orbitInclinationNeptune, axialTiltNeptune);
    _selectableModels[neptune->getID()] = neptune;
    _planets.push_back(neptune);

    // Pluto
    auto plutoTex = std::make_shared<Texture2D>(_ctx,
        AssetPath::getInstance()->get("textures/pluto/2k_pluto.jpg"), VK_FORMAT_R8G8B8A8_SRGB);
    auto pluto = std::make_shared<Planet>(_ctx, "Pluto", sphereDMesh, plutoTex,
        sizePluto, orbitRadPluto, orbitOffsetPluto, orbitVelocityPluto,
        spinVelocityPluto, orbitInclinationPluto, axialTiltPluto);
    _selectableModels[pluto->getID()] = pluto;
    _planets.push_back(pluto);

    // Orbits
    auto mercuryOrbit = std::make_shared<Orbit>(_ctx, "MercuryOrbit", quadDMesh, orbitRadMercury, orbitOffsetMercury, orbitVelocityMercury, orbitInclinationMercury);
    auto venusOrbit   = std::make_shared<Orbit>(_ctx, "VenusOrbit",   quadDMesh, orbitRadVenus,   orbitOffsetVenus,   orbitVelocityVenus,   orbitInclinationVenus);
    auto earthOrbit   = std::make_shared<Orbit>(_ctx, "EarthOrbit",   quadDMesh, orbitRadEarth,   orbitOffsetEarth,   orbitVelocityEarth,   orbitInclinationEarth);
    auto moonOrbit    = std::make_shared<Orbit>(_ctx, "MoonOrbit",    quadDMesh, orbitRadMoon,    orbitOffsetMoon,    orbitVelocityMoon,    orbitInclinationMoon);
    auto marsOrbit    = std::make_shared<Orbit>(_ctx, "MarsOrbit",    quadDMesh, orbitRadMars,    orbitOffsetMars,    orbitVelocityMars,    orbitInclinationMars);
    auto jupiterOrbit = std::make_shared<Orbit>(_ctx, "JupiterOrbit", quadDMesh, orbitRadJupiter, orbitOffsetJupiter, orbitVelocityJupiter, orbitInclinationJupiter);
    auto saturnOrbit  = std::make_shared<Orbit>(_ctx, "SaturnOrbit",  quadDMesh, orbitRadSaturn,  orbitOffsetSaturn,  orbitVelocitySaturn,  orbitInclinationSaturn);
    auto uranusOrbit  = std::make_shared<Orbit>(_ctx, "UranusOrbit",  quadDMesh, orbitRadUranus,  orbitOffsetUranus,  orbitVelocityUranus,  orbitInclinationUranus);
    auto neptuneOrbit = std::make_shared<Orbit>(_ctx, "NeptuneOrbit", quadDMesh, orbitRadNeptune, orbitOffsetNeptune, orbitVelocityNeptune, orbitInclinationNeptune);
    auto plutoOrbit   = std::make_shared<Orbit>(_ctx, "PlutoOrbit",   quadDMesh, orbitRadPluto,   orbitOffsetPluto,   orbitVelocityPluto,   orbitInclinationPluto);
    _orbits = { mercuryOrbit, venusOrbit, earthOrbit, moonOrbit, marsOrbit,
                jupiterOrbit, saturnOrbit, uranusOrbit, neptuneOrbit, plutoOrbit };

    _sunGlowSphere = std::make_shared<GlowSphere>(_ctx, "SunGlow", sphereDMesh,
        glm::vec4(1.f, 0.4f, 0.0f, 0.4f), 0.5f, 3.0f, sizeSun * 2.f, true);

    // Wire scene graph hierarchy
    _sun->addChild(_sunGlowSphere);
    _sun->addChild(mercuryOrbit);   _sun->addChild(_planets[0]);   // mercury
    _sun->addChild(venusOrbit);     _sun->addChild(_planets[1]);   // venus
    venus->addChild(venusGlow);
    _sun->addChild(earthOrbit);     _sun->addChild(_earth);
    _earth->addChild(earthGlow);
    _earth->addChild(moonOrbit);    _earth->addChild(_planets[3]); // moon
    _sun->addChild(marsOrbit);      _sun->addChild(_planets[4]);   // mars
    _sun->addChild(jupiterOrbit);   _sun->addChild(_planets[5]);   // jupiter
    _sun->addChild(saturnOrbit);    _sun->addChild(_planets[6]);   // saturn
    _sun->addChild(_planets[7]);                                   // saturn ring
    _sun->addChild(uranusOrbit);    _sun->addChild(_planets[8]);   // uranus
    _sun->addChild(neptuneOrbit);   _sun->addChild(_planets[9]);   // neptune
    _sun->addChild(plutoOrbit);     _sun->addChild(_planets[10]);  // pluto

    // Populate MultiPassRenderer model arrays
    // Glow pass: sun and sunGlowSphere use their own pipelines; planets are black occluders
    _glowPassModels.push_back({_sun,          glm::vec3(0.f), false});
    _glowPassModels.push_back({_sunGlowSphere, glm::vec3(0.f), false});
    for (const auto& planet : _planets)
        _glowPassModels.push_back({planet, glm::vec3(0.f), true});

    // Scene pass: skybox first, then sun, planets, glow spheres, orbits
    _sceneModels.push_back(_skyBox);
    _sceneModels.push_back(_sun);
    for (const auto& planet : _planets)
        _sceneModels.push_back(planet);
    for (const auto& gs : _glowSpheres)
        _sceneModels.push_back(gs);
    for (const auto& orbit : _orbits)
        _sceneModels.push_back(orbit);
}


void SolarSystem::createScenePipelines()
{
    VkDescriptorSetLayout sceneDSL = _sceneDescriptorSets[0]->getDescriptorSetLayout();

    // Planet pipeline
    PipelineParams pp;
    pp.name                  = "PlanetPipeline";
    pp.descriptorSetLayouts  = {sceneDSL, _planets[0]->getDescriptorSet()->getDescriptorSetLayout()};
    pp.pushConstantRanges    = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    pp.renderPass            = _mainRenderPass->getRenderPass();
    pp.msaaSamples           = _msaaSamples;
    _planetPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/planet/planet_vert.spv"),
        AssetPath::getInstance()->get("spv/planet/planet_frag.spv"), pp);

    // Ring pipeline
    pp.name              = "RingPipeline";
    pp.blendEnable       = true;
    pp.depthWrite        = false;
    pp.cullMode          = VK_CULL_MODE_NONE;
    _ringPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/ring/ring_vert.spv"),
        AssetPath::getInstance()->get("spv/ring/ring_frag.spv"), pp);
    pp.blendEnable = true;
    pp.depthWrite  = true;
    pp.cullMode    = VK_CULL_MODE_BACK_BIT;

    // Orbit pipeline
    pp.name                 = "OrbitPipeline";
    pp.descriptorSetLayouts = {sceneDSL};
    pp.pushConstantRanges   = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)}};
    pp.depthTest            = true;
    pp.depthWrite           = false;
    _orbitPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/orbit/orbit_vert.spv"),
        AssetPath::getInstance()->get("spv/orbit/orbit_frag.spv"), pp);
    pp.depthWrite = true;

    // GlowSphere pipeline
    pp.name                 = "GlowSpherePipeline";
    pp.descriptorSetLayouts = {sceneDSL, _glowSpheres[0]->getDescriptorSet()->getDescriptorSetLayout()};
    pp.pushConstantRanges   = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    pp.depthWrite           = false;
    pp.frontFace            = VK_FRONT_FACE_CLOCKWISE;
    _glowSpherePipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/glowsphere/glowsphere_vert.spv"),
        AssetPath::getInstance()->get("spv/glowsphere/glowsphere_frag.spv"), pp);
    pp.depthWrite = true;
    pp.frontFace  = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // SkyBox pipeline
    pp.name                 = "SkyBoxPipeline";
    pp.descriptorSetLayouts = {sceneDSL, _skyBox->getDescriptorSet()->getDescriptorSetLayout()};
    pp.pushConstantRanges   = {{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4)}};
    pp.depthWrite           = false;
    pp.frontFace            = VK_FRONT_FACE_CLOCKWISE;
    _skyBoxPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/skybox/skybox_vert.spv"),
        AssetPath::getInstance()->get("spv/skybox/skybox_frag.spv"), pp);
    pp.depthWrite = true;
    pp.frontFace  = VK_FRONT_FACE_COUNTER_CLOCKWISE;

    // Earth pipeline
    pp.name                 = "EarthPipeline";
    pp.descriptorSetLayouts = {sceneDSL, _earth->getDescriptorSet()->getDescriptorSetLayout()};
    pp.pushConstantRanges   = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    _earthPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/earth/earth_vert.spv"),
        AssetPath::getInstance()->get("spv/earth/earth_frag.spv"), pp);

    // Sun pipeline
    pp.name                 = "SunPipeline";
    pp.descriptorSetLayouts = {sceneDSL};
    pp.pushConstantRanges   = {{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4)}};
    _sunPipeline = std::make_unique<Pipeline>(_ctx,
        AssetPath::getInstance()->get("spv/sun/sun_vert.spv"),
        AssetPath::getInstance()->get("spv/sun/sun_frag.spv"), pp);
}


void SolarSystem::connectPipelines()
{
    _skyBox->setPipeline(_skyBoxPipeline);

    for (const auto& planet : _planets)
        planet->setPipeline(_planetPipeline);

    for (const auto& orbit : _orbits)
        orbit->setPipeline(_orbitPipeline);

    _sunGlowSphere->setPipeline(_glowSpherePipeline);
    for (const auto& gs : _glowSpheres)
        gs->setPipeline(_glowSpherePipeline);

    _sun->setPipeline(_sunPipeline);
    _earth->setPipeline(_earthPipeline);

    for (const auto& planet : _planets) {
        if (planet->getName() == "SaturnRing")
            planet->setPipeline(_ringPipeline);
    }
}


void SolarSystem::saveSceneState(const std::string& filename)
{
    std::ofstream outFile(filename);
    if (!outFile.is_open()) {
        spdlog::error("Failed to open file for saving scene state: {}", filename);
        return;
    }

    outFile << _sceneInfo.time << "\n";
    outFile << _currentTargetObjectID << "\n";
    outFile << _camera->getUp().x      << "," << _camera->getUp().y      << "," << _camera->getUp().z      << "\n";
    outFile << _camera->getForward().x << "," << _camera->getForward().y << "," << _camera->getForward().z << "\n";
    outFile << _camera->getLeft().x    << "," << _camera->getLeft().y    << "," << _camera->getLeft().z    << "\n";
    outFile << _camera->getTarget().x  << "," << _camera->getTarget().y  << "," << _camera->getTarget().z  << "\n";
    outFile << _camera->getRadius()    << "\n";

    outFile.close();
}


void SolarSystem::loadSceneState(const std::string& filename)
{
    std::ifstream inFile(filename);
    if (!inFile.is_open()) {
        spdlog::error("Failed to open file for loading scene state: {}", filename);
        return;
    }

    std::string line;

    std::getline(inFile, line); _sceneInfo.time = std::stof(line);
    std::getline(inFile, line); _currentTargetObjectID = std::stoul(line);

    auto readVec3 = [&](glm::vec3& v) {
        std::getline(inFile, line);
        sscanf(line.c_str(), "%f,%f,%f", &v.x, &v.y, &v.z);
    };

    glm::vec3 up, forward, left, target;
    readVec3(up);      _camera->setUp(up);
    readVec3(forward); _camera->setForward(forward);
    readVec3(left);    _camera->setLeft(left);
    readVec3(target);  _camera->setTarget(target);
    std::getline(inFile, line); _camera->setRadius(std::stof(line));

    inFile.close();
}
