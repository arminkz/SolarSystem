#include "Orbit.h"

#include "core/MultiPassRenderer.h"

Orbit::Orbit(std::shared_ptr<VulkanContext> ctx,
             std::string name,
             float orbitSize,
             float orbitOffset,
             float orbitVelocity,
             float orbitInclination)
    : Model(ctx, std::move(name), nullptr),
      _orbitSize(orbitSize),
      _orbitOffset(orbitOffset),
      _orbitVelocity(orbitVelocity),
      _orbitInclination(orbitInclination)
{
}


Orbit::~Orbit()
{
    // Cleanup resources if needed
}


void Orbit::computeLocalMatrix(float t)
{
    // Spin with the planet so it always sits at the circle's local +X, where the trail starts
    glm::mat4 spin = glm::rotate(glm::mat4(1.0f), glm::radians(_orbitOffset + _orbitVelocity * t), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(_orbitSize)); // unit circle -> orbit radius
    glm::mat4 tilt = glm::rotate(glm::mat4(1.0f), glm::radians(_orbitInclination), glm::vec3(1.0f, 0.0f, 0.0f));
    _localMatrix = tilt * scale * spin;
}


void Orbit::draw(VkCommandBuffer commandBuffer, const Renderer& renderer)
{
    if (!_visible) return;

    const MultiPassRenderer* ssScene = dynamic_cast<const MultiPassRenderer*>(&renderer);

    auto pipeline = _pipeline.lock();
    if (!pipeline) {
        spdlog::error("Pipeline is not set for Orbit model.");
        return;
    }

    pipeline->bind(commandBuffer);

    std::array<VkDescriptorSet, 1> descriptorSets = {
        ssScene->getSceneDescriptorSet()->getDescriptorSet() // Scene descriptor set
    };
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->getPipelineLayout(), 0, 1, descriptorSets.data(), 0, nullptr);

    PushConstants pc{};
    pc.model          = _modelMatrix;
    pc.lineWidth      = kLineWidth;
    pc.trailDirection = _orbitVelocity >= 0.0f ? 1.0f : -1.0f;
    pc.segments       = kSegments;
    vkCmdPushConstants(commandBuffer, pipeline->getPipelineLayout(), VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstants), &pc);

    // No vertex or index buffer: the vertex shader builds 6 vertices per segment
    vkCmdDraw(commandBuffer, 6 * kSegments, 1, 0, 0);
}
