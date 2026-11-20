#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

uniform vec3 uCamFwd, uCamRight, uCamUp; uniform vec2 uTanHalf; uniform float uShear;
uniform sampler2D uNoise;
uniform vec2 uCloudOff;
uniform float uCamAlt;          // the eye's height, for the far ridges' parallax
uniform float uFlat;            // the ladder's first rung: the clouds' fine layer fades to its mean
in vec2 vNdc;
out vec4 fragColor;
const vec3 RIDGE_D = vec3 (2600.0, 5200.0, 10000.0);
float ridgeTan (float az, float k) {
  float m = k < 0.5 ? 7.0 : (k < 1.5 ? 5.0 : 3.0);
  float row = 0.11 + 0.29 * k;
  float n = textureLod (uNoise, vec2 (az * m, row), 0.0).a * 0.78 + textureLod (uNoise, vec2 (az * m * 4.0, row + 0.13), 0.0).a * 0.22;
  float H = k < 0.5 ? 10.0 + 150.0 * n : (k < 1.5 ? 30.0 + 300.0 * n : 60.0 + 640.0 * n);
  float D = k < 0.5 ? RIDGE_D.x : (k < 1.5 ? RIDGE_D.y : RIDGE_D.z);
  return (H - uCamAlt) / D;
}
void main() {
  vec3 dir = normalize (uCamFwd + uCamRight * (vNdc.x * uTanHalf.x) + uCamUp * ((vNdc.y - uShear) * uTanHalf.y));
  vec3 col = skyColour (dir);
  float mu = dot (dir, uSun);
  float hl = max (length (dir.xz), 1.0e-4);
  float e = dir.y / hl;
  float az = atan (dir.x, -dir.z) * (1.0 / 6.2831853);
  // The thin high clouds: long streaks on a high deck, lit pink and gold from below by the low sun.
  float cloud = 0.0;
  if (dir.y > 0.008) {
    vec2 p = dir.xz / (dir.y + 0.06);
    vec2 q = vec2 (dot (p, vec2 (0.94, 0.34)), dot (p, vec2 (-0.34, 0.94))) * vec2 (0.07, 0.42) + uCloudOff;
    float n = texture (uNoise, q).r;
    float n2 = mix (texture (uNoise, q * 3.3 + vec2 (0.37, 0.11)).g, 0.5, uFlat);
    cloud = smoothstep (0.60, 0.84, n * 0.78 + n2 * 0.22) * 0.85;
    cloud *= smoothstep (0.008, 0.07, dir.y) * (1.0 - smoothstep (0.30, 0.75, dir.y));
    float near = pow (max (mu, 0.0), 6.0);
    vec3 lit = mix (vec3 (0.96, 0.58, 0.62), vec3 (1.40, 0.86, 0.50), near);
    lit = mix (lit, vec3 (0.80, 0.62, 0.78), smoothstep (0.15, 0.5, dir.y) * (1.0 - near));
    vec3 cc = mix (lit, lit * vec3 (0.66, 0.58, 0.74), smoothstep (0.55, 1.0, cloud) * 0.6);
    col = mix (col, cc, cloud * 0.85);
  }
  // The sun: a disc a little larger than life (a painter's), its corona.
  float th = sqrt (max (2.0 * (1.0 - mu), 0.0));
  col += vec3 (2.6, 2.0, 1.25) * (1.0 - smoothstep (0.0085, 0.0118, th)) * (1.0 - 0.75 * cloud);
  col += vec3 (1.0, 0.68, 0.32) * 0.40 * exp (-th * 30.0);
  // The far ridges, far to near: each nearer one over the farther; a warm rim along their crests toward the sun.
  if (e < 0.075) {
    float sunAz = pow (max (0.5 + 0.5 * dot (dir.xz / hl, uSunXZ), 0.0), 6.0);
    vec3 hc = hazeColour (dir);
    for (int k = 2; k >= 0; --k) {
      float kf = float (k);
      float crest = ridgeTan (az, kf);
      if (e < crest) {
        vec3 rc = ridgeFrom (dir, hc, kf);
        float rim = 1.0 - smoothstep (0.0, 0.0011 + 0.0004 * kf, crest - e);
        col = rc + vec3 (1.0, 0.70, 0.36) * rim * sunAz * (0.55 - 0.12 * kf);
      }
    }
  }
  // An attack: a faint ripple of oily colour through the sun's haze, the light shows of 1967. Nothing in silence.
  float hg = hitGlow ();
  if (hg > 0.001) {
    vec2 w = vec2 (az * 11.0, e * 9.0) + uTime * vec2 (0.013, 0.04);
    float oil = texture (uNoise, w).b;
    vec3 rainbow = 0.5 + 0.5 * cos (6.2831853 * (vec3 (0.0, 0.33, 0.67) + oil * 2.4 + uTime * 0.12));
    col += rainbow * pow (max (mu, 0.0), 5.0) * hg * 0.16 * smoothstep (0.25, 0.75, oil);
  }
  fragColor = vec4 (col, 1.0);
}
