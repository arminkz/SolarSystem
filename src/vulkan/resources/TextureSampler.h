#pragma once
#include "pch.h"
#include "vulkan/VulkanContext.h"

class TextureSampler
{
public:
    TextureSampler(std::shared_ptr<VulkanContext> ctx, uint32_t mipLevels, VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_REPEAT);
    ~TextureSampler();

    // Non-copyable and non-movable (owns raw Vulkan handles)
    TextureSampler(const TextureSampler&) = delete;
    TextureSampler& operator=(const TextureSampler&) = delete;
    TextureSampler(TextureSampler&&) = delete;
    TextureSampler& operator=(TextureSampler&&) = delete;

    VkSampler getSampler() const { return _textureSampler; }

private:
    std::shared_ptr<VulkanContext> _ctx;

    VkSampler _textureSampler;
};