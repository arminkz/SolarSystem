#pragma once

#include "stdafx.h"
#include "vulkan/VulkanContext.h"
#include "vulkan/Pipeline.h"
#include "vulkan/resources/Texture2D.h"
#include "geometry/DeviceMesh.h"
#include "Planet.h"

class Scene;
class SolarSystemScene;

class Earth : public Planet
{
public:
    Earth(std::shared_ptr<VulkanContext> ctx, 
          std::string name, 
          std::shared_ptr<DeviceMesh> mesh, 
          std::shared_ptr<Texture2D> baseColorTexture,
          std::shared_ptr<Texture2D> unlitColorTexture,
          std::shared_ptr<Texture2D> normalMapTexture,
          std::shared_ptr<Texture2D> specularTexture,
          std::shared_ptr<Texture2D> overlayColorTexture,
          float planetSize,
          float orbitRadius,
          float orbitAtT0,
          float orbitPerSec,
          float spinAtT0,
          float spinPerSec,
          float orbitInclination = 0.0f);

    ~Earth();

    void draw(VkCommandBuffer commandBuffer, const Renderer& renderer) override;
    void drawSelection(VkCommandBuffer commandBuffer, const Renderer& renderer) override;

    const DescriptorSet* getDescriptorSet() const { return _descriptorSet.get(); }

private:
    std::shared_ptr<Texture2D> _unlitColorTexture;
    std::shared_ptr<Texture2D> _normalMapTexture;
    std::shared_ptr<Texture2D> _specularTexture;
    std::shared_ptr<Texture2D> _overlayColorTexture;

    std::unique_ptr<DescriptorSet> _descriptorSet;
};