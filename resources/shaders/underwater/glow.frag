#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

uniform vec3 uColor; uniform float uMode;
in vec2 vQ; in vec2 vUV; in vec2 vUV1; in vec3 vRel;
out vec4 fragColor;
void main() {
  float I;
  if (uMode < 0.5) I = exp (-3.5 * dot (vQ, vQ));
  else { float t = vQ.x * 0.5 + 0.5; I = pow (1.0 - t, 1.6) * exp (-2.5 * vQ.y * vQ.y) * smoothstep (0.0, 0.1, t); }
  fragColor = vec4 (uColor * I * (1.0 - fogAmount (length (vRel))), 0.0);
}
