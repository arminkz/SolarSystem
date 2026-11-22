#version 450

layout(location = 0) noperspective in float fragEdge; // signed distance from the line centre, in pixels
layout(location = 1) in float fragTrail;              // 0 right behind the planet, 1 a full orbit behind it
layout(location = 2) flat in float fragHalfWidth;     // half the line width, in pixels

layout(location = 0) out vec4 outColor;

const float TRAIL_LENGTH = 0.5;  // fraction of the orbit covered by the fading trail
const float PEAK_ALPHA   = 0.12; // opacity right behind the planet
const float BASE_ALPHA   = 0.0;  // opacity of the rest of the orbit

void main() {
    // How much of this pixel the line covers: 1 inside, falling to 0 over one pixel at the edge
    float coverage = clamp(fragHalfWidth + 0.5 - abs(fragEdge), 0.0, 1.0);

    float trail = 1.0 - clamp(fragTrail / TRAIL_LENGTH, 0.0, 1.0);
    float alpha = coverage * mix(BASE_ALPHA, PEAK_ALPHA, trail);

    // Don't touch the target where nothing is drawn (blending would still overwrite its alpha)
    if (alpha <= 0.0) discard;

    outColor = vec4(1.0, 1.0, 1.0, alpha);
}
