#pragma once

#include "pch.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/Pipeline.h"
#include "core/Renderer.h"
#include "scene/Model.h"


// An orbit drawn as an antialiased line of constant pixel width.
// It has no mesh: the vertex shader (shaders/orbit/orbit.vert) generates the circle.
class Orbit : public Model
{
public:
    // Must match the push_constant block in orbit.vert
    struct PushConstants {
        glm::mat4 model;
        float lineWidth;      // in pixels
        float trailDirection; // +1 or -1 (sign of the orbit velocity)
        uint32_t segments;
    };

    Orbit(std::shared_ptr<VulkanContext> ctx,
          std::string name,
          float orbitSize,
          float orbitOffset,
          float orbitVelocity,
          float orbitInclination = 0.0f);

    ~Orbit();

    void draw(VkCommandBuffer commandBuffer, const Renderer& renderer) override;

    void computeLocalMatrix(float t) override;

protected:
    // Enough that the line stays on the planet when zoomed in: with 1024 segments the largest
    // orbit (radius 218) is off the true circle by at most 0.001 units between vertices.
    static constexpr uint32_t kSegments  = 1024;
    static constexpr float    kLineWidth = 1.0f; // in pixels

    float _orbitSize = 1.0f;           // Orbit radius
    float _orbitOffset = 0.0f;         // Initial orbit angle
    float _orbitVelocity = 0.0f;       // Orbit speed
    float _orbitInclination = 0.0f;    // Orbital inclination (degrees)
};
