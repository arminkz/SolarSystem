#pragma once

#include "stdafx.h"

#include "vulkan/resources/Texture2D.h"
#include "vulkan/Pipeline.h"
#include "vulkan/DescriptorSet.h"
#include "core/Renderer.h"
#include "scene/Model.h"
#include "scene/SelectableModel.h"

class Renderer;
class SolarSystemRenderer;

class Planet : public SelectableModel
{
public:
    Planet(std::shared_ptr<VulkanContext> ctx, 
           std::string name, 
           std::shared_ptr<DeviceMesh> mesh,
           std::shared_ptr<Texture2D> baseColorTexture,
           std::weak_ptr<Model> parent,
           float planetSize,
           float orbitRadius,
           float orbitAtT0,
           float orbitPerSec,
           float spinAtT0,
           float spinPerSec);
           
    ~Planet();

    void draw(VkCommandBuffer commandBuffer, const Renderer& renderer) override;
    void drawSelection(VkCommandBuffer commandBuffer, const Renderer& scene) override;

    const DescriptorSet* getDescriptorSet() const { return _descriptorSet.get(); }

    void calculateModelMatrix(float t);

protected:
    std::weak_ptr<Model> _parent;             //Weak pointer to parent planet (if any)

    float _size = 1.0f;                        //Used for scaling the model
    float _orbitRadius = 0.0f;
    float _orbitAtT0 = 0.0f;
    float _orbitPerSec = 0.0f;
    float _spinAtT0 = 0.0f;
    float _spinPerSec = 0.0f;

    std::shared_ptr<Texture2D> _baseColorTexture;
    std::unique_ptr<DescriptorSet> _descriptorSet;

};