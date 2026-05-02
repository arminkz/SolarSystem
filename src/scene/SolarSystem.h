#pragma once

#include "pch.h"
#include "core/MultiPassRenderer.h"
#include "models/Planet.h"
#include "models/Sun.h"
#include "models/Earth.h"
#include "models/Orbit.h"
#include "models/GlowSphere.h"
#include "models/SkyBox.h"


class SolarSystem : public MultiPassRenderer
{
public:
    SolarSystem(std::shared_ptr<VulkanContext> ctx, std::shared_ptr<SwapChain> swapChain);
    ~SolarSystem();

    void advance() override final;
    void buildUI() override;
    void handleKeyDown(int key, int scancode, int modifiers) override;

private:
    // Scene pipelines
    std::shared_ptr<Pipeline> _planetPipeline;
    std::shared_ptr<Pipeline> _orbitPipeline;
    std::shared_ptr<Pipeline> _glowSpherePipeline;
    std::shared_ptr<Pipeline> _skyBoxPipeline;
    std::shared_ptr<Pipeline> _sunPipeline;
    std::shared_ptr<Pipeline> _earthPipeline;
    std::shared_ptr<Pipeline> _ringPipeline;

    void createModels();
    void createScenePipelines();
    void connectPipelines();

    // Models
    std::vector<std::shared_ptr<Planet>> _planets;
    std::vector<std::shared_ptr<Orbit>>  _orbits;
    std::vector<std::shared_ptr<GlowSphere>> _glowSpheres;
    std::shared_ptr<SkyBox>              _skyBox;
    std::shared_ptr<Sun>                 _sun;
    std::shared_ptr<GlowSphere>          _sunGlowSphere;
    std::shared_ptr<Earth>               _earth;

    // Simulation state
    float     _timeScale    = 1.0f;
    bool      _isPaused     = false;
    bool      _showUI       = false;
    TimePoint _lastFrameTime = std::chrono::high_resolution_clock::now();

    // Bloom settings (local copy for UI sliders)
    float _bloomScale    = 2.0f;
    float _bloomStrength = 1.5f;

    bool _showOrbits = true;

    // Save / load
    void saveSceneState(const std::string& filename);
    void loadSceneState(const std::string& filename);

    // -------------------------------------------------------------------------
    // Planet sizes
    // -------------------------------------------------------------------------
    const float sizeSun        = 3.f;
    const float sizeMercury    = 0.21f;
    const float sizeVenus      = 0.48f;
    const float sizeEarth      = 0.54f;
    const float sizeMoon       = 0.1f;
    const float sizeMars       = 0.36f;
    const float sizeJupiter    = 0.8f;
    const float sizeSaturn     = 0.7f;
    const float sizeSaturnRing = 1.2f * sizeSaturn;
    const float sizeUranus     = 0.4f;
    const float sizeNeptune    = 0.4f;
    const float sizePluto      = 0.2f;

    // -------------------------------------------------------------------------
    // Orbit radii
    // -------------------------------------------------------------------------
    const float orbitRadMercury =  16.f;
    const float orbitRadVenus   =  24.f;
    const float orbitRadEarth   =  34.f;
    const float orbitRadMoon    =   2.f;
    const float orbitRadMars    =  44.f;
    const float orbitRadJupiter =  90.f;
    const float orbitRadSaturn  = 126.f;
    const float orbitRadUranus  = 160.f;
    const float orbitRadNeptune = 192.f;
    const float orbitRadPluto   = 218.f;

    // -------------------------------------------------------------------------
    // Angle offset at t=0 (degrees)
    // -------------------------------------------------------------------------
    const float orbitOffsetMercury = 252.25f;
    const float orbitOffsetVenus   = 181.97f;
    const float orbitOffsetEarth   = 100.46f;
    const float orbitOffsetMoon    =   0.f;
    const float orbitOffsetMars    = 355.45f;
    const float orbitOffsetJupiter =  34.40f;
    const float orbitOffsetSaturn  =  49.94f;
    const float orbitOffsetUranus  = 313.23f;
    const float orbitOffsetNeptune = 304.88f;
    const float orbitOffsetPluto   = 238.92f;

    // -------------------------------------------------------------------------
    // Orbit speeds (degrees per second)
    // -------------------------------------------------------------------------
    const float orbitVelocityMercury = 0.0040f;
    const float orbitVelocityVenus   = 0.0016f;
    const float orbitVelocityEarth   = 0.0009f;
    const float orbitVelocityMoon    = 0.01f;
    const float orbitVelocityMars    = 0.0005f;
    const float orbitVelocityJupiter = 0.00008f;
    const float orbitVelocitySaturn  = 0.00003f;
    const float orbitVelocityUranus  = 0.00001f;
    const float orbitVelocityNeptune = 0.000006f;
    const float orbitVelocityPluto   = 0.000004f;


    // -------------------------------------------------------------------------
    // Spin speeds (degrees per second)
    // -------------------------------------------------------------------------
    const float spinVelocityMercury =  0.006f;
    const float spinVelocityVenus   = -0.0015f;
    const float spinVelocityEarth   =  0.329f;
    const float spinVelocityMoon    =  0.01f;
    const float spinVelocityMars    =  0.335f;
    const float spinVelocityJupiter =  0.838f;
    const float spinVelocitySaturn  =  0.727f;
    const float spinVelocityUranus  = -0.427f;
    const float spinVelocityNeptune =  0.538f;
    const float spinVelocityPluto   = -0.057f;

    // -------------------------------------------------------------------------
    // Axial tilt / obliquity (degrees) — tilt of spin axis relative to orbital plane
    // -------------------------------------------------------------------------
    const float axialTiltMercury =   0.03f;
    const float axialTiltVenus   = 177.36f;
    const float axialTiltEarth   =  23.44f;
    const float axialTiltMoon    =   6.68f;
    const float axialTiltMars    =  25.19f;
    const float axialTiltJupiter =   3.13f;
    const float axialTiltSaturn  =  26.73f;
    const float axialTiltUranus  =  97.77f;
    const float axialTiltNeptune =  28.32f;
    const float axialTiltPluto   = 122.53f;

    // -------------------------------------------------------------------------
    // Orbital inclinations (degrees)
    // -------------------------------------------------------------------------
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
};
