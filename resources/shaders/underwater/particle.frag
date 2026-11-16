#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

uniform float uKind;
in vec2 vUv; in float vA; in vec3 vCol; in float vF;
out vec4 fragColor;
void main() {
  vec2 d = vUv * 2.0 - 1.0; float r = length (d);
  float a;
  vec3 c = vCol;
  if (uKind > 1.5) {        // a bubble: a bright rim, a highlight up and left, a faint lens of the water in it
    float ring = smoothstep (0.60, 0.82, r) * (1.0 - smoothstep (0.88, 1.0, r));
    float hi = smoothstep (0.30, 0.0, length (d - vec2 (-0.32, 0.36)));
    float lens = (1.0 - smoothstep (0.0, 0.75, r)) * 0.10;
    a = (ring * 0.75 + hi * 1.0 + lens) * vA;
    c = mix (vCol, waterColour (vec3 (0.0, 1.0, 0.0)), 0.25);
  } else {
    a = smoothstep (1.0, 0.25, r) * vA;
    if (uKind > 0.5) a *= 0.7 + 0.3 * smoothstep (0.6, 0.0, r);
  }
  fragColor = vec4 (c * a, a);
}
