#include "plains/height.glsl"

in vec2 aGrid;
uniform vec2 uLvlOrigin; uniform float uCellSize, uMorphOn;
uniform mat4 uViewProj;
out vec3 vPos; out vec4 vA, vB, vC;
void main() {
  vec2 pr = uLvlOrigin + aGrid * uCellSize;
  float h = terrainH (pr, vA, vB, vC);
  vec2 par = mod (aGrid, 2.0);
  vec2 d = par * uCellSize;
  float hc = 0.5 * (terrainH (pr - d) + terrainH (pr + d));   // the height on the coarser lattice
  float b = max (abs (aGrid.x - 64.0), abs (aGrid.y - 64.0));
  float alpha = clamp ((b - 48.0) / 15.0, 0.0, 1.0) * uMorphOn;
  h = mix (h, hc, alpha);
  vPos = vec3 (pr.x, h, pr.y);
  gl_Position = uViewProj * vec4 (vPos, 1.0);
}
