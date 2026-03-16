#include "GlowSphere.h"

#include "scene/SolarSystemRenderer.h"

GlowSphere::GlowSphere(std::shared_ptr<VulkanContext> ctx, 
                         std::string name, 
                         std::shared_ptr<DeviceMesh> mesh,
                         std::weak_ptr<Model> parent,
                         glm::vec4 color,
                         float coeffScatter,
                         float powScatter,
                         float planetSize,
                         bool isLightSource)
    : Model(ctx, std::move(name), std::move(mesh)), _parent(std::move(parent)), _size(planetSize)
{
    _glowSphereInfo.color = color;
    _glowSphereInfo.coeffScatter = coeffScatter;
    _glowSphereInfo.powScatter = powScatter;
    _glowSphereInfo.isLightSource = isLightSource ? 1 : 0;
    _glowSphereUBO = std::make_unique<Buffer>(_ctx, sizeof(GlowSphereInfo), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    _glowSphereUBO->copyData(&_glowSphereInfo, sizeof(_glowSphereInfo));

    std::vector<Descriptor> descriptors = {
        Descriptor(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, _glowSphereUBO->getDescriptorInfo())
    };
    _descriptorSet = std::make_unique<DescriptorSet>(_ctx, descriptors);
}


GlowSphere::~GlowSphere()
{
    // Cleanup resources if needed
}


void GlowSphere::calculateModelMatrix()
{
    // GlowSphere has the same position as the parent planet
    glm::vec3 parentPosition = glm::vec3(0.0f);
    if (auto parent = _parent.lock()) {
        parentPosition = parent->getModelMatrix() * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    // Update the model matrix
    _modelMatrix = glm::translate(glm::mat4(1.0f), parentPosition) * glm::scale(glm::mat4(1.0f), glm::vec3(_size));
}


void GlowSphere::draw(VkCommandBuffer commandBuffer, const Renderer& renderer)
{
    const SolarSystemRenderer* ssScene = dynamic_cast<const SolarSystemRenderer*>(&renderer);

    auto pipeline = _pipeline.lock();
    if (!pipeline) {
        spdlog::error("Pipeline is not set for GlowSphere model.");
        return;
    }

    // Bind the pipeline and descriptor set
    pipeline->bind(commandBuffer);
    
    VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, _mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    std::array<VkDescriptorSet, 2> descriptorSets = {
        ssScene->getSceneDescriptorSet()->getDescriptorSet(),
        _descriptorSet->getDescriptorSet()  // Per-model descriptor set
    };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), 0, 2, descriptorSets.data(), 0, nullptr);

    // Push constants for model
    vkCmdPushConstants(commandBuffer, pipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4), &_modelMatrix);

    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(_mesh->getIndicesCount()), 1, 0, 0, 0);
}