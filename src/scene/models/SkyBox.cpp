#include "SkyBox.h"

#include "core/MultiPassRenderer.h"


SkyBox::SkyBox(std::shared_ptr<VulkanContext> ctx, 
               std::string name, 
               std::shared_ptr<DeviceMesh> mesh, 
               std::shared_ptr<TextureCubemap> cubemapTexture)
    : Model(std::move(ctx), std::move(name), std::move(mesh)), 
      _cubemapTexture(std::move(cubemapTexture))
{
    std::vector<Descriptor> descriptors = {
        Descriptor(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1, _cubemapTexture->getDescriptorInfo()), // Cubemap texture
    };
    _descriptorSet = std::make_unique<DescriptorSet>(_ctx, descriptors);

    _modelMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(1500.0f)); // Scale the skybox to a large size
}


SkyBox::~SkyBox()
{
    // Cleanup resources if needed
}


void SkyBox::draw(VkCommandBuffer commandBuffer, const Renderer& renderer)
{
    const MultiPassRenderer* ssScene = dynamic_cast<const MultiPassRenderer*>(&renderer);

    auto pipeline = _pipeline.lock();
    if (!pipeline) {
        spdlog::error("Pipeline is not set for SkyBox model.");
        return;
    }

    // Bind the pipeline and descriptor set
    pipeline->bind(commandBuffer);
    
    VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, _mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    std::array<VkDescriptorSet, 2> descriptorSets = {
        ssScene->getSceneDescriptorSet()->getDescriptorSet(),        // Scene descriptor set
        _descriptorSet->getDescriptorSet()                           // My descriptor set
    };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), 0, 2, descriptorSets.data(), 0, nullptr);

    // Push constants for model
    vkCmdPushConstants(commandBuffer, pipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(glm::mat4), &_modelMatrix);

    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(_mesh->getIndicesCount()), 1, 0, 0, 0);
}