// From the prototype's metal-shaders.js (FS_STEAM), its body as it is.
#include "metal/ftex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

uniform sampler2D uDetail;
in vec3 vCol; in vec2 vUv; in float vA; in vec2 vSeed;
out vec4 fragColor;
void main() {
  float r = length (vUv - 0.5) * 2.0;
  float tex = texture (uDetail, vUv * 0.6 + vSeed).b;
  float a = vA * (1.0 - smoothstep (0.2, 1.0, r)) * (0.6 + 0.8 * tex);
  fragColor = vec4 (vCol * a, a);
}
