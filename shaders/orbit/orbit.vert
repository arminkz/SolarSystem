#version 450

// Draws an orbit as a ring of screen-space quads. There is no vertex buffer: every vertex is
// generated from gl_VertexIndex, 6 per segment (two triangles).

// Per-frame variables (set 0 is per-frame descriptor set)
layout(set = 0, binding = 0) uniform SceneInfo {
    mat4 view;
    mat4 proj;

    float time;
    vec3 cameraPosition;
    vec3 lightColor;
    vec2 viewportSize; // in pixels
} si;

// Must match Orbit::PushConstants
layout(push_constant) uniform PushConstants {
    mat4 model;           // unit circle in the local XZ plane -> world (radius, inclination, spin)
    float lineWidth;      // in pixels
    float trailDirection; // +1 or -1: which way the trail runs from the planet (sign of orbit velocity)
    uint segments;        // number of segments around the orbit
} pc;

layout(location = 0) noperspective out float fragEdge; // signed distance from the line centre, in pixels
layout(location = 1) out float fragTrail;              // 0 right behind the planet, 1 a full orbit behind it
layout(location = 2) flat out float fragHalfWidth;     // half the line width, in pixels

const float PI = 3.14159265359;
const float AA_MARGIN = 1.0;  // extra pixels on each side of the line for the antialiased edge
const float NEAR_EPS = 1e-4;  // keep clipped endpoints just in front of the near plane

// (endpoint, side) for the 6 corners of a segment's two triangles
const vec2 CORNERS[6] = vec2[](
    vec2(0.0, -1.0), vec2(1.0, -1.0), vec2(1.0, 1.0),
    vec2(0.0, -1.0), vec2(1.0,  1.0), vec2(0.0, 1.0)
);

void main() {
    uint segment = uint(gl_VertexIndex) / 6u;
    vec2 corner = CORNERS[gl_VertexIndex % 6];

    // Endpoints as fractions of a full orbit. The planet sits at fraction 0 (local +X):
    // Orbit::computeLocalMatrix spins the circle with the planet, so it stays there.
    float f0 = float(segment) / float(pc.segments);
    float f1 = float(segment + 1u) / float(pc.segments);

    mat4 mvp = si.proj * si.view * pc.model;
    vec4 c0 = mvp * vec4(cos(2.0 * PI * f0), 0.0, sin(2.0 * PI * f0), 1.0);
    vec4 c1 = mvp * vec4(cos(2.0 * PI * f1), 0.0, sin(2.0 * PI * f1), 1.0);

    // Clip the segment against the near plane (z = 0 in Vulkan clip space) before the
    // perspective divide. A segment reaching behind the camera would otherwise flip across the screen.
    if (c0.z < NEAR_EPS && c1.z < NEAR_EPS) {
        gl_Position = vec4(0.0, 0.0, -1.0, 1.0); // whole segment behind the camera: degenerate, clipped
        fragEdge = 0.0;
        fragTrail = 0.0;
        fragHalfWidth = 0.0;
        return;
    }
    if (c0.z < NEAR_EPS) {
        float t = (NEAR_EPS - c0.z) / (c1.z - c0.z);
        c0 = mix(c0, c1, t);
        f0 = mix(f0, f1, t);
    } else if (c1.z < NEAR_EPS) {
        float t = (NEAR_EPS - c1.z) / (c0.z - c1.z);
        c1 = mix(c1, c0, t);
        f1 = mix(f1, f0, t);
    }

    // Segment direction and normal in pixels
    vec2 halfViewport = 0.5 * si.viewportSize;
    vec2 p0 = c0.xy / c0.w * halfViewport;
    vec2 p1 = c1.xy / c1.w * halfViewport;
    vec2 dir = p1 - p0;
    float len = length(dir);
    dir = len > 1e-6 ? dir / len : vec2(1.0, 0.0);
    vec2 normal = vec2(-dir.y, dir.x);

    // Push this corner sideways by a fixed number of pixels, whatever the depth
    float halfWidth = 0.5 * pc.lineWidth;
    float extent = halfWidth + AA_MARGIN;
    vec4 clipPos = corner.x == 0.0 ? c0 : c1;
    clipPos.xy += normal * (corner.y * extent) / halfViewport * clipPos.w;

    float f = mix(f0, f1, corner.x);

    gl_Position = clipPos;
    fragEdge = corner.y * extent;
    fragTrail = pc.trailDirection > 0.0 ? f : 1.0 - f;
    fragHalfWidth = halfWidth;
}
