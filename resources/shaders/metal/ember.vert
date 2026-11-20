// From the prototype's metal-shaders.js (VS_EMBER), its body as it is.
#include "metal/vtex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

uniform mat4 uProj; uniform vec2 uRes; uniform float uTravel, uSpeed, uEmber;
out float vA; out vec2 vUv;
void main() {
  int id = gl_VertexID; uint pi = uint (id / 6); int ci = id - int (pi) * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  float rx = hs (pi, 1u), ry = hs (pi, 2u), rz = hs (pi, 3u), rw = hs (pi, 4u);
  if (hs (pi, 5u) > uEmber) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vA = 0.0; vUv = vec2 (0.0); return; }
  const float range = 60.0;
  float z = fract (rz - (uTravel * 1.0 + uTime * 1.2) / range) * range + 2.0;
  float y = fract (ry + uTime * 0.03 * (0.5 + rw)) * 7.0 - 3.0 + 0.4 * sin (uTime * 1.7 + rw * 30.0);
  vec3 P = vec3 ((rx * 2.0 - 1.0) * 28.0 + 0.6 * sin (uTime * 1.1 + rw * 20.0), y, -z);
  float len = min (0.2 + uSpeed * 0.05, z - 1.5);
  vec3 P2 = P + vec3 (0.0, 0.0, len);
  vec4 a = uProj * vec4 (P, 1.0), b = uProj * vec4 (P2, 1.0);
  vec2 na = a.xy / a.w, nb = b.xy / b.w;
  vec2 hres = uRes * 0.5;
  vec2 dpx = (nb - na) * hres; float L = length (dpx); vec2 dirpx = dpx / max (L, 1e-3);
  vec2 perp = vec2 (-dirpx.y, dirpx.x);
  float thick = 1.3 + 9.0 / z;
  vec2 ppx = mix (na, nb, corner.x) * hres + perp * (corner.y - 0.5) * thick;
  gl_Position = vec4 (ppx / hres, 0.0, 1.0);
  vA = (0.35 + 0.65 * rw) * (0.12 + 0.5 * (1.0 - smoothstep (6.0, 60.0, z)));
  vUv = corner;
}
