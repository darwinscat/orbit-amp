// From the prototype's io-shaders.js (VS_SNOW), its body as it is.
#include "io/vtex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

uniform mat4 uProj; uniform vec2 uRes; uniform float uTravel, uSpeed, uSnow, uCount;
uniform vec3 uSunV; uniform vec3 uLavaEnv;      // the Sun in camera space; the lava's light round the rover
out vec4 vC; out vec2 vUv;
void main() {
  int id = gl_VertexID; uint pi = uint (id / 6); int ci = id - int (pi) * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  float rx = hs (pi, 1u), ry = hs (pi, 2u), rz = hs (pi, 3u), rw = hs (pi, 4u), rs = hs (pi, 5u);
  float fadeIn = clamp ((uSnow * uCount - float (pi)) / (0.06 * uCount), 0.0, 1.0);
  if (fadeIn <= 0.0) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vC = vec4 (0.0); vUv = vec2 (0.0); return; }
  const float range = 40.0;
  float fall = 2.2 + 1.6 * rw;
  float z = fract (rz - uTravel / range) * range + 0.8;
  float y = fract (ry - uTime * fall / 14.0) * 14.0 - 6.0;
  vec3 P = vec3 ((rx * 2.0 - 1.0) * 24.0 * (0.3 + 0.7 * z / range), y, -z);
  float expo = 0.02;
  vec3 P2 = P + vec3 (0.0, fall * expo, min (uSpeed * expo, z - 0.4));
  vec4 a = uProj * vec4 (P, 1.0), b = uProj * vec4 (P2, 1.0);
  vec2 na = a.xy / a.w, nb = b.xy / b.w;
  vec2 hres = uRes * 0.5;
  vec2 pa = na * hres, pb = nb * hres;
  vec2 dpx = pb - pa; float L = length (dpx);
  float size = (0.012 + 0.026 * rs * rs) / z * hres.y / 0.633;
  float cover = min (1.0, size / 0.9);
  size = max (size, 0.9);
  vec2 dirpx = (L > 1e-3) ? dpx / L : vec2 (1.0, 0.0);
  vec2 perp = vec2 (-dirpx.y, dirpx.x);
  vec2 ppx = mix (pa - dirpx * size, pb + dirpx * size, corner.x) + perp * (corner.y - 0.5) * 2.0 * size;
  gl_Position = vec4 (ppx / hres, 0.0, 1.0);
  // tumbling: a facet turns to the Sun now and then
  float glint = pow (max (sin (uTime * (2.0 + 5.0 * rs) + rw * 60.0), 0.0), 30.0);
  // the rover's lamps: what falls through their beams ahead glitters in them (in the dark it is most of what shows)
  vec2 off = vec2 (P.x, P.y + 1.0) / max (-P.z, 0.5);
  float beam = (1.0 - smoothstep (0.22, 0.42, length (off * vec2 (0.8, 1.3)))) / (1.0 + sq (z / 16.0));
  vec3 lit = uSunCol * uSunK * ioTerm (uCamRel) * (0.55 + 0.9 * max (dot (normalize (P), uSunV), 0.0) + 5.0 * glint) + uJupShine * 1.2 + uLavaEnv * (1.5 + 4.0 * glint)
           + vec3 (0.75, 0.80, 0.90) * beam * (1.3 + 4.0 * glint) + vec3 (0.010, 0.012, 0.018);
  float thin = min (1.0, (2.0 * size) / max (L + 2.0 * size, 1.0)) * 0.6 + 0.4;
  float A = fadeIn * (0.35 + 0.45 * rw) * smoothstep (0.8, 2.0, z) * (0.35 + 0.65 * smoothstep (range, 6.0, z)) * thin * cover;
  vC = vec4 (vec3 (0.96, 0.93, 0.78) * lit * A, A * 0.5);
  vUv = corner;
}
