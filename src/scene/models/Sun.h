#pragma once

#include "pch.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/Pipeline.h"
#include "core/Renderer.h"
#include "geometry/DeviceMesh.h"
#include "scene/Model.h"
#include "scene/SelectableModel.h"

class Sun : public SelectableModel
{
public:
    Sun(std::shared_ptr<VulkanContext> ctx, 
        std::string name, 
        std::shared_ptr<DeviceMesh> mesh,
        float planetSize);

    ~Sun();

    void draw(VkCommandBuffer commandBuffer, const Renderer& renderer) override;
    void drawSelection(VkCommandBuffer commandBuffer, const Renderer& renderer) override;

    void computeLocalMatrix(float t) override;

    // Used in Bloom effect
    const glm::vec3 glowColor = glm::vec3(1.f, 0.3f, 0.0f);

protected:

    float _size = 1.0f;
};