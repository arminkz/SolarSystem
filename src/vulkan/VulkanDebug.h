#pragma once

#include "pch.h"

namespace vkdebug {
    // VK_EXT_debug_utils function pointers (declarations only).
    // These are loaded at runtime in VulkanContext::loadDebugUtilsFunctions()
    // and stay nullptr when the extension is not available.
    extern PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT;
    extern PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT;
    extern PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT;

    // Command-buffer labels: group commands into named, colored ranges in RenderDoc / Nsight.
    void beginLabel(VkCommandBuffer cb, const char* name, glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f});
    void endLabel(VkCommandBuffer cb);

    // Object names: show "RingPipeline" instead of "Pipeline 0x3f" in tools and validation messages.
    void setName(VkDevice device, uint64_t handle, VkObjectType type, const std::string& name);

    // All helpers are no-ops when the extension is missing, so they are safe to leave in Release.
}
