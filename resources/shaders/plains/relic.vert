#include "plains/height.glsl"

in vec3 aPos; in vec3 aNrm; in vec3 aCol; in float aKind; in vec2 aCell;
uniform mat4 uViewProj;
uniform vec3 uCamRel;
const float RC = 532.0; const int RM = 1250;    // the relic cell, WORLD / RC
out vec3 vN; out vec3 vRel; out vec3 vCol; out float vDust;
void main() {
  ivec2 cij = ivec2 (aCell);
  ivec2 c = (cij + RM * 64) % RM;
  vec2 foot = (vec2 (cij) + 0.2 + 0.6 * vec2 (hcell (c, 51u), hcell (c, 52u))) * RC - uCamCell;
  float ang = 6.2831853 * hcell (c, 54u); float ca = cos (ang), sa = sin (ang);
  bool track = aKind > 1.5;
  vDust = track ? 0.0 : (0.35 + 0.5 * hcell (c, 55u)) * (aKind > 0.5 ? 0.4 : 1.0);   // the panels, smoother, hold less of it
  vec3 lp = aPos; vec3 ln = aNrm;
  if (track && hcell (c, 56u) > 0.6) lp = vec3 (0.0);
  lp.xz = vec2 (ca * lp.x - sa * lp.z, sa * lp.x + ca * lp.z);
  ln.xz = vec2 (ca * ln.x - sa * ln.z, sa * ln.x + ca * ln.z);
  vec3 wp = vec3 (foot.x + lp.x, 0.0, foot.y + lp.z);
  wp.y = track ? terrainH (wp.xz) + 0.05 : terrainH (foot) + lp.y;
  vN = ln; vRel = wp - uCamRel; vCol = aCol;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
