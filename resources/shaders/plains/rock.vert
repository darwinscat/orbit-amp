#include "plains/height.glsl"

in vec3 aPos; in vec3 aNrm;
uniform mat4 uViewProj;
uniform ivec2 uRockBase; uniform int uRockW, uRockM; uniform float uRockCell, uRockProb; uniform vec2 uRockSize;
uniform vec3 uCamRel;
out vec3 vN; out vec3 vRel; out float vTint;
void main() {
  int ii = gl_InstanceID;
  ivec2 cij = ivec2 (ii % uRockW, ii / uRockW) + uRockBase;
  ivec2 c = (cij + uRockM * 8) % uRockM;
  float r0 = hcell (c, 11u);
  int variant = (hcell (c, 19u) < 0.5) ? 0 : 1;
  int myVar = gl_VertexID / 240;
  vec2 cen = (vec2 (cij) + vec2 (hcell (c, 12u), hcell (c, 13u))) * uRockCell;
  vec2 pr = cen - uCamCell;
  float m = HTEX (uTexOff0 + pr / L0).r;
  if (r0 > uRockProb || variant != myVar || m > 0.62) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vN = vec3 (0, 1, 0); vRel = vec3 (0); vTint = 0.0; return; }
  float gh = terrainH (pr);
  float s = mix (uRockSize.x, uRockSize.y, pow (hcell (c, 14u), 2.2));
  vec3 sc = s * vec3 (0.7 + 0.6 * hcell (c, 15u), 0.5 + 0.5 * hcell (c, 16u), 0.7 + 0.6 * hcell (c, 17u));
  float ang = 6.2831853 * hcell (c, 18u); float ca = cos (ang), sa = sin (ang);
  vec3 lp = aPos * sc; vec3 ln = normalize (aNrm / sc);
  lp.xz = vec2 (ca * lp.x - sa * lp.z, sa * lp.x + ca * lp.z);
  ln.xz = vec2 (ca * ln.x - sa * ln.z, sa * ln.x + ca * ln.z);
  vec3 wp = vec3 (pr.x + lp.x, gh + lp.y - 0.3 * sc.y, pr.y + lp.z);
  vN = ln; vRel = wp - uCamRel; vTint = hcell (c, 20u);
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
