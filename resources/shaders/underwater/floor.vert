#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#include "underwater/common.glsl"

in vec2 aGrid;
uniform vec2 uLvlOrigin; uniform float uCellSize;
uniform mat4 uViewProj;
out vec3 vPos; out vec3 vN; out vec4 vA, vB, vC;
void main() {
  vec2 pr = uLvlOrigin + aGrid * uCellSize;
  float h = floorH (pr, vA, vB, vC);
  float e = 1.5;
  float hx = floorH (pr + vec2 (e, 0.0)), hz = floorH (pr + vec2 (0.0, e));
  vN = normalize (vec3 (h - hx, e, h - hz));
  vPos = vec3 (pr.x, h, pr.y);
  gl_Position = uViewProj * vec4 (vPos, 1.0);
}
