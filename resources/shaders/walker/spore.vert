#include "walker/common.glsl"

uniform mat4 uProj; uniform vec2 uResolution, uTanHalf; uniform float uTravel, uSpeed, uSpores, uCount;
out float vA; out vec2 vUv; out vec3 vCol;
void main () {
  int id = gl_VertexID; uint pi = uint (id / 6); int ci = id - int (pi) * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  float rx = hs (pi, 1u), ry = hs (pi, 2u), rz = hs (pi, 3u), rw = hs (pi, 4u), rs = hs (pi, 5u), rh = hs (pi, 6u);
  const float range = 44.0;
  float z = fract (rz - (uTravel + uTime * 0.3) / range) * range + 0.6;
  float vy = 0.10;
  float y = fract (ry + uTime * vy / 22.0) * 22.0 - 11.0;
  float sway = (0.4 + 0.6 * rs) * sin (uTime * (0.4 + 0.6 * rw) + rw * 20.0);
  float gust = uStorm * 2.5 * sin (uTime * 0.9 + rz * 11.0 + rw * 3.0);
  vec3 P = vec3 ((rx * 2.0 - 1.0) * 28.0 * (0.3 + 0.7 * z / range) + sway + gust, y, -z);
  float expo = 0.022;
  vec3 P2 = P + vec3 (gust * 0.1 * uStorm, -vy * expo, min (uSpeed * expo, z - 0.35));
  vec4 a = uProj * vec4 (P, 1.0), b = uProj * vec4 (P2, 1.0);
  vec2 na = a.xy / a.w, nb = b.xy / b.w;
  vec2 hres = uResolution * 0.5;
  vec2 pa = na * hres, pb = nb * hres;
  vec2 dpx = pb - pa; float L = length (dpx);
  float size = (0.006 + 0.012 * rs * rs) * (1.0 + 0.6 * uStorm) / z * hres.y / uTanHalf.y;
  size = clamp (size, 0.9, 16.0);
  vec2 dirpx = (L > 1e-3) ? dpx / L : vec2 (1.0, 0.0);
  vec2 perp = vec2 (-dirpx.y, dirpx.x);
  vec2 ppx = mix (pa - dirpx * size, pb + dirpx * size, corner.x) + perp * (corner.y - 0.5) * 2.0 * size;
  gl_Position = vec4 (ppx / hres, 0.0, 1.0);
  float fadeIn = clamp ((uSpores * uCount - float (pi)) / (0.1 * uCount), 0.0, 1.0);
  float thin = min (1.0, (2.0 * size) / max (L + 2.0 * size, 1.0)) * 0.6 + 0.4;
  float twinkle = 0.7 + 0.3 * sin (uTime * (2.0 + 3.0 * rw) + rw * 40.0);
  vA = fadeIn * (0.35 + 0.65 * rw) * smoothstep (0.6, 1.8, z) * (0.3 + 0.7 * smoothstep (range, 5.0, z)) * thin * twinkle;
  vA *= 0.7 + 0.5 * uBreath + 0.9 * uStorm;
  vUv = corner;
  vCol = palette (floor (rh * 3.0) + step (0.97, rh) * 3.0);
}
