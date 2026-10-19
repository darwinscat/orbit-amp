uniform mat4 uProj; uniform vec2 uResolution; uniform float uTravel, uTime, uSpeed, uSnow, uCount;
out float vA; out vec2 vUv;
#include "common/ih.glsl"
#include "common/hs.glsl"
void main() {
  int id = gl_VertexID; uint pi = uint (id / 6); int ci = id - int (pi) * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  float rx = hs (pi, 1u), ry = hs (pi, 2u), rz = hs (pi, 3u), rw = hs (pi, 4u), rs = hs (pi, 5u);
  const float range = 36.0, fall = 1.1;
  float z = fract (rz - (uTravel + uTime * 0.35) / range) * range + 0.7;        // metres ahead, flowing toward the glass
  float sway = 0.4 * sin (uTime * (0.6 + rw) + rw * 20.0);
  float y = fract (ry - uTime * fall / 12.0) * 12.0 - 6.5;
  vec3 P = vec3 ((rx * 2.0 - 1.0) * 22.0 * (0.35 + 0.65 * z / range) + sway, y, -z);
  // The streak: where this flake was ~1/45 s ago (it came toward us at the rover's speed, and fell).
  float expo = 0.022;
  vec3 P2 = P + vec3 (0.0, fall * expo, min (uSpeed * expo, z - 0.4));
  vec4 a = uProj * vec4 (P, 1.0), b = uProj * vec4 (P2, 1.0);
  vec2 na = a.xy / a.w, nb = b.xy / b.w;
  vec2 hres = uResolution * 0.5;
  vec2 pa = na * hres, pb = nb * hres;
  vec2 dpx = pb - pa; float L = length (dpx);
  float size = (0.012 + 0.03 * rs * rs) / z * hres.y / 0.23087;   // the flake's radius in pixels
  size = max (size, 0.9);
  vec2 dirpx = (L > 1e-3) ? dpx / L : vec2 (1.0, 0.0);
  vec2 perp = vec2 (-dirpx.y, dirpx.x);
  vec2 ppx = mix (pa - dirpx * size, pb + dirpx * size, corner.x) + perp * (corner.y - 0.5) * 2.0 * size;
  gl_Position = vec4 (ppx / hres, 0.0, 1.0);
  float fadeIn = clamp ((uSnow * uCount - float (pi)) / (0.12 * uCount), 0.0, 1.0);
  float thin = min (1.0, (2.0 * size) / max (L + 2.0 * size, 1.0)) * 0.6 + 0.4;   // long streaks are fainter
  vA = fadeIn * (0.45 + 0.45 * rw) * smoothstep (0.7, 2.0, z) * (0.35 + 0.65 * smoothstep (range, 6.0, z)) * thin;
  vUv = corner;
}
