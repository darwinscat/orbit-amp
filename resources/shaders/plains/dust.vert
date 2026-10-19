uniform mat4 uProj; uniform vec2 uResolution; uniform float uTravel, uTime, uSpeed, uStorm;
out float vA; out vec2 vUv;
#include "common/ih.glsl"
#include "common/hs.glsl"
void body() {
  int id = gl_VertexID; uint pi = uint (id / 6); int ci = id - int (pi) * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  float rx = hs (pi, 1u), ry = hs (pi, 2u), rz = hs (pi, 3u), rw = hs (pi, 4u);
  if (hs (pi, 5u) > uStorm) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vA = 0.0; vUv = vec2 (0.0); return; }   // a light dust is sparse, not faint
  const float range = 70.0;
  float z = fract (rz - (uTravel * 1.2 + uTime * 9.0) / range) * range + 2.5;   // metres ahead, flowing toward the eye
  vec3 P = vec3 ((rx * 2.0 - 1.0) * 30.0, (ry * 2.0 - 1.0) * 6.5 - 0.5, -z);
  float len = min (0.4 + uSpeed * 0.09, z - 1.4);
  vec3 P2 = P + vec3 (0.0, 0.0, len);                       // the nearer end of the streak
  vec4 a = uProj * vec4 (P, 1.0), b = uProj * vec4 (P2, 1.0);
  vec2 na = a.xy / a.w, nb = b.xy / b.w;
  vec2 hres = uResolution * 0.5;
  vec2 dpx = (nb - na) * hres; float L = length (dpx); vec2 dirpx = dpx / max (L, 1e-3);
  vec2 perp = vec2 (-dirpx.y, dirpx.x);
  float thick = 1.4 + 22.0 / z;
  vec2 ppx = mix (na, nb, corner.x) * hres + perp * (corner.y - 0.5) * thick;
  gl_Position = vec4 (ppx / hres, 0.0, 1.0);
  vA = min (1.0, 0.4 + 0.6 * uStorm) * (0.10 + 0.42 * smoothstep (70.0, 8.0, z)) * (0.5 + 0.5 * rw);
  vUv = corner;
}

// Attribute 0, always 0 (gpu::attributeless): read, so a browser sees the program take an attribute and does not
// emulate one for it — adding it changes no position.
in float aZero;
void main() { body(); gl_Position.x += aZero; }
