#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

in vec3 vRel; in float vSide; in float vCov;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  float across = 1.0 - abs (vSide * 2.0 - 1.0);
  float a = vCov * smoothstep (0.0, 0.45, across) * (1.0 - hazeAmount (dist) * 0.8);
  float glint = pow (max (dot (V, uSun), 0.0), 40.0);
  vec3 c = vec3 (0.07, 0.06, 0.06) + uSunCol * glint * 1.6;
  fragColor = vec4 (c * a, a);
}
