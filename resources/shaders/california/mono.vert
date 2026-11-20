#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define LTEX(uv) textureLod(uLand, uv, 0.0)
#include "california/common.glsl"
#include "california/light.glsl"

uniform mat4 uViewProj;
uniform vec3 uMonoC;                 // the foot's centre (rel)
uniform vec3 uMonoX, uMonoZ;         // its half axes on the ground (across the broad face, through the thickness)
uniform float uMonoH, uMonoFade;
out vec3 vRel; out vec3 vN; out vec2 vFace;
void main() {
  int f = gl_VertexID / 6, k = gl_VertexID % 6;
  int c = (k == 0 || k == 3) ? 0 : (k == 1) ? 1 : (k == 2 || k == 4) ? 2 : 3;      // two triangles: 0 1 2, 0 2 3
  vec2 q = vec2 ((c == 1 || c == 2) ? 1.0 : -1.0, (c >= 2) ? 1.0 : -1.0);
  vec3 n, p;      // in the slab's frame: x across the broad face, y up 0..1, z through the thickness
  if (f == 0)      { n = vec3 (0.0, 0.0, 1.0);  p = vec3 (q.x, q.y * 0.5 + 0.5, 1.0); }
  else if (f == 1) { n = vec3 (0.0, 0.0, -1.0); p = vec3 (-q.x, q.y * 0.5 + 0.5, -1.0); }
  else if (f == 2) { n = vec3 (1.0, 0.0, 0.0);  p = vec3 (1.0, q.y * 0.5 + 0.5, -q.x); }
  else if (f == 3) { n = vec3 (-1.0, 0.0, 0.0); p = vec3 (-1.0, q.y * 0.5 + 0.5, q.x); }
  else             { n = vec3 (0.0, 1.0, 0.0);  p = vec3 (q.x, 1.0, -q.y); }
  vec3 wp = uMonoC + uMonoX * p.x + vec3 (0.0, uMonoH * uMonoFade * p.y - 0.3, 0.0) + uMonoZ * p.z;
  vN = normalize (uMonoX) * n.x + vec3 (0.0, n.y, 0.0) + normalize (uMonoZ) * n.z;
  vRel = wp - uCamRel;
  vFace = q;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
