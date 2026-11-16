#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#include "underwater/common.glsl"

in vec3 aPos; in vec3 aNrm;
uniform mat4 uViewProj;
uniform ivec2 uRockBase; uniform int uRockW, uRockM; uniform float uRockCell, uRockProb; uniform vec2 uRockSize;
uniform float uTall;      // 0: a boulder; 1: a spire, stretched up
out vec3 vN; out vec3 vRel; out vec3 vPos; out float vTint; out float vLocalY;
void main() {
  int ii = gl_InstanceID;
  ivec2 cij = ivec2 (ii % uRockW, ii / uRockW) + uRockBase;
  ivec2 c = (cij + uRockM * 8) % uRockM;
  float r0 = hcell (c, 11u);
  int variant = (hcell (c, 19u) < 0.5) ? 0 : 1;
  int myVar = gl_VertexID / 240;
  vec2 cen = (vec2 (cij) + vec2 (hcell (c, 12u), hcell (c, 13u))) * uRockCell;
  vec2 pr = cen - uCamCell;
  vec4 a, b, cc; float gh = floorH (pr, a, b, cc);
  float fieldOk = uTall > 0.5 ? smoothstep (0.55, 0.7, b.r) : 1.0;
  if (r0 > uRockProb * fieldOk || variant != myVar) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vN = vec3 (0, 1, 0); vRel = vec3 (0); vPos = vec3 (0); vTint = 0.0; vLocalY = 0.0; return; }
  float s = mix (uRockSize.x, uRockSize.y, pow (hcell (c, 14u), 2.0));
  vec3 sc = s * vec3 (0.7 + 0.6 * hcell (c, 15u), 0.5 + 0.5 * hcell (c, 16u), 0.7 + 0.6 * hcell (c, 17u));
  if (uTall > 0.5) sc = s * vec3 (0.22 + 0.12 * hcell (c, 15u), 1.0 + 0.9 * hcell (c, 16u), 0.22 + 0.12 * hcell (c, 17u));
  float ang = 6.2831853 * hcell (c, 18u); float ca = cos (ang), sa = sin (ang);
  vec3 lp = aPos * sc; vec3 ln = normalize (aNrm / sc);
  lp.xz = vec2 (ca * lp.x - sa * lp.z, sa * lp.x + ca * lp.z);
  ln.xz = vec2 (ca * ln.x - sa * ln.z, sa * ln.x + ca * ln.z);
  float sink = uTall > 0.5 ? 0.55 * sc.y : 0.3 * sc.y;
  vec3 wp = vec3 (pr.x + lp.x, gh + lp.y - sink, pr.y + lp.z);
  vN = ln; vRel = wp - uCamRel; vPos = wp; vTint = hcell (c, 20u); vLocalY = (lp.y - sink) / max (sc.y, 0.01);
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
