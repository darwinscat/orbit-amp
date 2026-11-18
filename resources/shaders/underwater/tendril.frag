#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

in vec3 vRel; in vec3 vCol; in vec3 vGlow; in float vT; in float vSide; in float vPhase; in float vAlpha;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  float across = 1.0 - abs (vSide * 2.0 - 1.0);
  float pulse = pow (0.5 + 0.5 * sin (vT * 15.0 - uTime * 3.5 - vPhase), 9.0);
  float base = (1.0 - vT) * (1.0 - vT);
  float a = (0.30 * base + 0.9 * pulse * (1.0 - vT * 0.6)) * across * vAlpha;
  float f = fogAmount (dist);
  vec3 col = (vCol * 0.35 * base + vGlow * (0.35 * base + 1.6 * pulse) * hitK (dist)) * across * vAlpha * (1.0 - f);
  fragColor = vec4 (col, a * 0.45 * (1.0 - f));
}
