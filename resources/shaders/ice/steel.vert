#include "ice/height.glsl"

in vec3 aPos; in vec3 aNrm; in vec3 aExp; in float aLod; in vec2 aCell;
uniform mat4 uViewProj;
uniform int uVariant;
uniform vec3 uCamRel; uniform float uPxExpand;   // metres of bar width per metre of distance that one pixel covers
out vec3 vN; out vec3 vRel; out vec3 vWorld; out float vSeed;
void main() {
  ivec2 cij = ivec2 (aCell);
  vec2 fcell; float rad; int variant; float scl, ang;
  bool ok = structCell (cij, fcell, rad, variant, scl, ang);
  vec2 pr = (vec2 (cij) + fcell) * SC - uCamCell;
  if (! ok || variant != uVariant) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vN = vec3 (0, 1, 0); vRel = vec3 (0); vWorld = vec3 (0); vSeed = 0.0; return; }
  vec4 b = HTEX (uTexOff1 + pr / L1);
  if (chaosMaskOf (b) > 0.02) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vN = vec3 (0, 1, 0); vRel = vec3 (0); vWorld = vec3 (0); vSeed = 0.0; return; }
  float gh = terrainH (pr);
  float dist = length (vec3 (pr.x, gh, pr.y) - uCamRel);
  float ca = cos (ang), sa = sin (ang);
  vec3 lp = aPos * scl;
  // Thin bars kept at least ~1.3 px wide: expanded across the bar with distance; the detail bars vanish far away.
  float extra = max (0.0, uPxExpand * dist * 1.3 - 0.22 * scl) * 0.5;
  lp += aExp * extra;
  if (aLod > 0.5 && dist > 900.0) lp = vec3 (0.0, 0.0, 0.0);
  vec3 ln = aNrm;
  lp.xz = vec2 (ca * lp.x - sa * lp.z, sa * lp.x + ca * lp.z);
  ln.xz = vec2 (ca * ln.x - sa * ln.z, sa * ln.x + ca * ln.z);
  vec3 wp = vec3 (pr.x + lp.x, gh + lp.y - 0.15, pr.y + lp.z);
  vN = ln; vRel = wp - uCamRel; vWorld = vec3 (wp.x + uCamCell.x, wp.y, wp.z + uCamCell.y); vSeed = hcell (cij, 48u);
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
