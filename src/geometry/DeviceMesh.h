#pragma once

#include "pch.h"
#include "vulkan/VulkanContext.h"
#include "geometry/HostMesh.h"

// Mesh representation on GPU
class DeviceMesh
{
public:
    DeviceMesh(std::shared_ptr<VulkanContext> ctx, const HostMesh& mesh);
    ~DeviceMesh();

    uint32_t getIndicesCount() const { return _indexCount; }
    VkBuffer getVertexBuffer() const { return _vertexBuffer; }
    VkBuffer getIndexBuffer() const { return _indexBuffer; }
    
private:
    std::shared_ptr<VulkanContext> _ctx;

    VkBuffer _vertexBuffer;
    VkDeviceMemory _vertexBufferMemory;
    VkBuffer _indexBuffer;
    VkDeviceMemory _indexBufferMemory;
    uint32_t _indexCount;

    void createVertexBuffer(const HostMesh& mesh);
    void createIndexBuffer(const HostMesh& mesh);
};