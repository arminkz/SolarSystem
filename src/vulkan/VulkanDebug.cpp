#include "vulkan/VulkanDebug.h"

#include <cstring>

namespace vkdebug {
    PFN_vkCmdBeginDebugUtilsLabelEXT vkCmdBeginDebugUtilsLabelEXT = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT vkCmdEndDebugUtilsLabelEXT = nullptr;
    PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT = nullptr;

    void beginLabel(VkCommandBuffer cb, const char* name, glm::vec4 color) {
        if (!vkCmdBeginDebugUtilsLabelEXT) return;
        VkDebugUtilsLabelEXT label{VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT};
        label.pLabelName = name;
        std::memcpy(label.color, &color, sizeof(label.color));
        vkCmdBeginDebugUtilsLabelEXT(cb, &label);
    }

    void endLabel(VkCommandBuffer cb) {
        if (vkCmdEndDebugUtilsLabelEXT) vkCmdEndDebugUtilsLabelEXT(cb);
    }

    void setName(VkDevice device, uint64_t handle, VkObjectType type, const std::string& name) {
        if (!vkSetDebugUtilsObjectNameEXT || name.empty() || handle == 0) return;
        VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
        info.objectType   = type;
        info.objectHandle = handle;
        info.pObjectName  = name.c_str();
        vkSetDebugUtilsObjectNameEXT(device, &info);
    }
}
