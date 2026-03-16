#include "DeviceMesh.h"

#include "vulkan/VulkanHelper.h"

DeviceMesh::DeviceMesh(std::shared_ptr<VulkanContext> ctx, const HostMesh& mesh)
    : _ctx(std::move(ctx))
{
    _indexCount = static_cast<uint32_t>(mesh.indices.size());
    createVertexBuffer(mesh);
    createIndexBuffer(mesh);
}

DeviceMesh::~DeviceMesh() {
    vkDestroyBuffer(_ctx->device, _vertexBuffer, nullptr);
    vkFreeMemory(_ctx->device, _vertexBufferMemory, nullptr);

    vkDestroyBuffer(_ctx->device, _indexBuffer, nullptr);
    vkFreeMemory(_ctx->device, _indexBufferMemory, nullptr);
}

void DeviceMesh::createVertexBuffer(const HostMesh& mesh)
{
        // Create Vertex Buffer
        VkDeviceSize bufferSize = sizeof(mesh.vertices[0]) * mesh.vertices.size(); // Size of the vertex buffer

        // Create staging buffer which is visible by both GPU and CPU
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;
        VulkanHelper::createBuffer(_ctx,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            false, stagingBuffer, stagingBufferMemory);
    
        void* data;
        // Map the buffer memory into CPU addressable space
        vkMapMemory(_ctx->device, stagingBufferMemory, 0, bufferSize, 0, &data);
        // Copy the vertex data to the mapped memory
        memcpy(data, mesh.vertices.data(), (size_t)bufferSize);
        // Unmap the memory
        vkUnmapMemory(_ctx->device, stagingBufferMemory); 
    
        // Create the vertex buffer
        VulkanHelper::createBuffer(_ctx,
            bufferSize,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            false, _vertexBuffer, _vertexBufferMemory);
    
        // Copy the data from the staging buffer to the vertex buffer
        VulkanHelper::copyBuffer(_ctx, stagingBuffer, _vertexBuffer, bufferSize);
    
        // Cleanup staging buffer
        vkDestroyBuffer(_ctx->device, stagingBuffer, nullptr); // Destroy the staging buffer
        vkFreeMemory(_ctx->device, stagingBufferMemory, nullptr); // Free the staging buffer memory
}

void DeviceMesh::createIndexBuffer(const HostMesh& mesh)
{
    // Create Index Buffer
    VkDeviceSize bufferSize = sizeof(mesh.indices[0]) * mesh.indices.size(); // Size of the index buffer

    // Create staging buffer which is visible by both GPU and CPU
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;
    VulkanHelper::createBuffer(_ctx,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        false, stagingBuffer, stagingBufferMemory);

    void* data;
    // Map the buffer memory into CPU addressable space
    vkMapMemory(_ctx->device, stagingBufferMemory, 0, bufferSize, 0, &data);
    // Copy the index data to the mapped memory
    memcpy(data, mesh.indices.data(), (size_t)bufferSize);
    // Unmap the memory
    vkUnmapMemory(_ctx->device, stagingBufferMemory); 

    // Create the index buffer
    VulkanHelper::createBuffer(_ctx,
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        false, _indexBuffer, _indexBufferMemory);

    // Copy the data from the staging buffer to the index buffer
    VulkanHelper::copyBuffer(_ctx, stagingBuffer, _indexBuffer, bufferSize);

    // Cleanup staging buffer
    vkDestroyBuffer(_ctx->device, stagingBuffer, nullptr); // Destroy the staging buffer
    vkFreeMemory(_ctx->device, stagingBufferMemory, nullptr); // Free the staging buffer memory
}