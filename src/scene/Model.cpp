#include "Model.h"


Model::Model(std::shared_ptr<VulkanContext> ctx, std::string name, std::shared_ptr<DeviceMesh> mesh)
    : _ctx(std::move(ctx)), _name(std::move(name)), _mesh(std::move(mesh))
{
    // Initialize model matrix to identity
    _modelMatrix = glm::mat4(1.0f);
}
