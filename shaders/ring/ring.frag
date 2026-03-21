#version 450

layout(set = 0, binding = 0) uniform SceneInfo {
    mat4 view;
    mat4 proj;
    float time;
    vec3 cameraPosition;
    vec3 lightColor;
} si;

layout(push_constant) uniform PushConstants {
    mat4 model;
} pc;

layout(set = 1, binding = 0) uniform sampler2D baseColorTexture;

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec2 fragTexCoord;
layout(location = 2) in vec4 worldPosition;
layout(location = 3) in vec3 worldNormal;
layout(location = 4) in vec3 worldTangent;
layout(location = 5) in vec3 positionView;
layout(location = 6) in vec3 normalView;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 texColor = texture(baseColorTexture, fragTexCoord);

    // Simple distance-based lighting: ring brightness depends on sun distance
    vec3 ringPos = pc.model[3].xyz;
    float dist = length(ringPos);
    float light = clamp(1.0 / (1.0 + dist * 0.002), 0.3, 1.0);

    outColor = vec4(texColor.rgb * light, texColor.a);
}
