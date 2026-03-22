#include "Sun.h"

#include "core/MultiPassRenderer.h"


Sun::Sun(std::shared_ptr<VulkanContext> ctx, 
         std::string name, 
         std::shared_ptr<DeviceMesh> mesh,
         float planetSize)
    : SelectableModel(std::move(ctx), std::move(name), std::move(mesh)), 
      _size(planetSize)
{
}


Sun::~Sun()
{
    // Cleanup resources if needed
}


void Sun::computeLocalMatrix(float /*t*/)
{
    _localMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(_size));
}


void Sun::draw(VkCommandBuffer commandBuffer, const Renderer& renderer)
{
    const MultiPassRenderer* ssScene = dynamic_cast<const MultiPassRenderer*>(&renderer);

    auto pipeline = _pipeline.lock();
    if (!pipeline) {
        spdlog::error("Pipeline is not set for Sun model.");
        return;
    }

    // Bind the pipeline and descriptor set
    pipeline->bind(commandBuffer);

    VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, _mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    std::array<VkDescriptorSet, 1> descriptorSets = {
        ssScene->getSceneDescriptorSet()->getDescriptorSet() // Scene descriptor set
    };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), 0, 1, descriptorSets.data(), 0, nullptr);

    // Push constants for model
    vkCmdPushConstants(commandBuffer, pipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(glm::mat4), &_modelMatrix);

    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(_mesh->getIndicesCount()), 1, 0, 0, 0);
}


void Sun::drawSelection(VkCommandBuffer commandBuffer, const Renderer& renderer)
{
    const MultiPassRenderer* ssScene = dynamic_cast<const MultiPassRenderer*>(&renderer);

    auto selectionPipeline = _selectionPipeline.lock();
    if (!selectionPipeline) {
        spdlog::error("Selection pipeline is not set for Sun model.");
        return;
    }
    
    // Bind the pipeline and descriptor set
    selectionPipeline->bind(commandBuffer);

    VkBuffer vertexBuffers[] = {_mesh->getVertexBuffer()};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, _mesh->getIndexBuffer(), 0, VK_INDEX_TYPE_UINT32);

    std::array<VkDescriptorSet, 1> descriptorSets = {
        ssScene->getSceneDescriptorSet()->getDescriptorSet(),
    };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, selectionPipeline->getPipelineLayout(), 0, 1, descriptorSets.data(), 0, nullptr);

    // Push constants for selection
    struct PushConstants {
        glm::mat4 model;
        int objectID;
    } pushConstants{};

    pushConstants.model = _modelMatrix;
    pushConstants.objectID = getID();
    vkCmdPushConstants(commandBuffer, selectionPipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants), &pushConstants);

    vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(_mesh->getIndicesCount()), 1, 0, 0, 0);
}