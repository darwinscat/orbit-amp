#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#include "underwater/common.glsl"

in vec2 aGrid;
uniform vec2 uLvlOrigin; uniform float uCellSize;
// At the foot, the lattice's odd vertices slide onto the even ones below them over this band of distance (metres,
// across the floor), so beyond it the lattice is the coarse one of 2 x 2 cells, drawn as such: no crack, no pop.
uniform vec2 uMorph;
uniform mat4 uViewProj;
out vec3 vPos; out vec3 vN; out vec4 vA, vB, vC;
void main() {
  float k = clamp ((length (uLvlOrigin + aGrid * uCellSize - uCamFrac) - uMorph.x) / (uMorph.y - uMorph.x), 0.0, 1.0);
  vec2 g = aGrid - mod (aGrid, 2.0) * k;
  vec2 pr = uLvlOrigin + g * uCellSize;
  float h = floorH (pr, vA, vB, vC);
  float e = 1.5;
  float hx = floorH (pr + vec2 (e, 0.0)), hz = floorH (pr + vec2 (0.0, e));
  vN = normalize (vec3 (h - hx, e, h - hz));
  vPos = vec3 (pr.x, h, pr.y);
  gl_Position = uViewProj * vec4 (vPos, 1.0);
}
