#include "ice/height.glsl"

// THE LAMPS' LIGHT: a quad facing the eye over each lamp of one variant (uLampAt, in its structure's own frame),
// placed as ice/steel.vert places the structure; a glow of uGlowPx at least, more for a lamp that is near.
in vec2 aCell;
uniform int uVariant;
uniform mat4 uViewProj;
uniform vec3 uCamRel; uniform vec3 uLampAt; uniform float uLampSize;
uniform vec2 uPxNdc;      // one pixel in NDC, across and up
uniform float uMetrePx;   // the pixels a metre covers at a distance of one
uniform float uGlowPx, uFadeK;
out vec2 vUv; out float vFade;
void main() {
  int k = gl_VertexID;
  vUv = vec2 ((k == 1 || k == 2 || k == 4) ? 1.0 : -1.0, (k == 2 || k == 4 || k == 5) ? 1.0 : -1.0);
  vFade = 0.0;
  ivec2 cij = ivec2 (aCell);
  vec2 fcell; float rad; int variant; float scl, ang;
  bool ok = structCell (cij, fcell, rad, variant, scl, ang);
  vec2 pr = (vec2 (cij) + fcell) * SC - uCamCell;
  if (! ok || variant != uVariant || chaosMaskOf (HTEX (uTexOff1 + pr / L1)) > 0.02) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); return; }
  float gh = terrainH (pr);
  float ca = cos (ang), sa = sin (ang);
  vec3 lp = uLampAt * scl;
  lp.xz = vec2 (ca * lp.x - sa * lp.z, sa * lp.x + ca * lp.z);
  vec3 wp = vec3 (pr.x + lp.x, gh + lp.y - 0.15, pr.y + lp.z);
  vec3 toEye = uCamRel - wp; float dist = length (toEye);
  wp += toEye / dist * min (1.0, 0.5 * dist);      // toward the eye, so the lamp's own steel does not hide its light
  vec4 c = uViewProj * vec4 (wp, 1.0);
  float r = max (uGlowPx, 3.0 * uLampSize * scl * uMetrePx / c.w);
  c.xy += vUv * r * uPxNdc * c.w;
  vFade = exp (-dist * uFadeK);
  gl_Position = c;
}
