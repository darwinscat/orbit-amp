#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#include "underwater/common.glsl"

uniform mat4 uViewProj;
uniform vec3 uCenter, uAxisA, uAxisB;
uniform vec4 uRect0, uRect1;        // two atlas cells (the nearest headings): u0, v0 (top), u1, v1 (bottom)
out vec2 vQ; out vec2 vUV; out vec2 vUV1; out vec3 vRel;
void main() {
  int i = gl_VertexID;
  vec2 q = vec2 ((i == 1 || i == 2 || i == 4) ? 1.0 : -1.0, (i == 2 || i == 4 || i == 5) ? 1.0 : -1.0);
  vec3 p = uCenter + uAxisA * q.x + uAxisB * q.y;
  vQ = q;
  vUV = vec2 (mix (uRect0.x, uRect0.z, q.x * 0.5 + 0.5), mix (uRect0.w, uRect0.y, q.y * 0.5 + 0.5));
  vUV1 = vec2 (mix (uRect1.x, uRect1.z, q.x * 0.5 + 0.5), mix (uRect1.w, uRect1.y, q.y * 0.5 + 0.5));
  vRel = p - uCamRel;
  gl_Position = uViewProj * vec4 (p, 1.0);
}
