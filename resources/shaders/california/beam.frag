#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

uniform float uBeamA;
in vec3 vRel; in vec3 vN; in float vH;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  float facing = abs (dot (normalize (vN.xz), normalize (V.xz + vec2 (1e-5))));
  float a = uBeamA * 0.16 * pow (facing, 1.3) * (1.0 - 0.55 * vH) * (0.8 + 0.2 * sin (uTime * 3.0 - vH * 16.0));
  a *= 1.0 - hazeAmount (dist) * 0.8;
  fragColor = vec4 (vec3 (0.80, 1.0, 0.88) * a, 0.0);
}
