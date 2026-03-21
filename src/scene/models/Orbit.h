#pragma once

#include "stdafx.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/Pipeline.h"
#include "core/Renderer.h"
#include "geometry/DeviceMesh.h"
#include "scene/Model.h"


class Orbit : public Model
{
public:
    Orbit(std::shared_ptr<VulkanContext> ctx,
          std::string name,
          std::shared_ptr<DeviceMesh> mesh,
          float orbitSize,
          float orbitAtT0,
          float orbitPerSec);

    ~Orbit();

    void draw(VkCommandBuffer commandBuffer, const Renderer& renderer) override;

    void computeLocalMatrix(float t) override;

protected:
    float _orbitSize = 1.0f;           // Used for scaling the model
    float _orbitAtT0 = 0.0f;           // Initial orbit angle
    float _orbitPerSec = 0.0f;         // Orbit speed
};