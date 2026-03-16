#pragma once

#include "stdafx.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/Pipeline.h"
#include "vulkan/DescriptorSet.h"
#include "vulkan/resources/Buffer.h"
#include "core/Renderer.h"
#include "geometry/DeviceMesh.h"
#include "scene/Model.h"


class GlowSphere : public Model
{
public:
    GlowSphere(std::shared_ptr<VulkanContext> ctx, 
               std::string name, 
               std::shared_ptr<DeviceMesh> mesh,
               std::weak_ptr<Model> parent,
               glm::vec4 color,
               float coeffScatter = 3.0f,
               float powScatter = 3.0f,
               float planetSize = 1.0f,
               bool isLightSource = false);

    ~GlowSphere();

    void draw(VkCommandBuffer commandBuffer, const Renderer& renderer) override;

    const DescriptorSet* getDescriptorSet() const { return _descriptorSet.get(); }

    void calculateModelMatrix();

private:

    std::weak_ptr<Model> _parent;
    float _size = 1.0f;

    struct GlowSphereInfo {
        alignas(16) glm::vec4 color;
        alignas(4) float coeffScatter = 3.0f;
        alignas(4) float powScatter = 3.0f;
        alignas(4) int isLightSource = 0;
    } _glowSphereInfo;
    std::unique_ptr<Buffer> _glowSphereUBO;

    std::unique_ptr<DescriptorSet> _descriptorSet;
};