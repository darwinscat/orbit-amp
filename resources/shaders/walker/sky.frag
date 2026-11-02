#include "walker/common.glsl"
#include "walker/light.glsl"

uniform vec3 uCamFwd, uCamRight, uCamUp; uniform vec2 uTanHalf; uniform float uShear, uSunInt;
in vec2 vNdc;
out vec4 fragColor;
void main () {
  vec3 dir = normalize (uCamFwd + uCamRight * (vNdc.x * uTanHalf.x) + uCamUp * ((vNdc.y - uShear) * uTanHalf.y));
  float t = clamp (dir.y, 0.0, 1.0);
  vec3 hor = horizonAt (dir);
  float band = 1.0 - exp (-t * 14.0);
  vec3 col = mix (hor, uSkyZen, band);
  float s = max (dot (dir, uSunDir), 0.0);
  col += uSunGlow * (pow (s, 60.0) * 0.6 + pow (s, 10.0) * 0.14);
  float disc = smoothstep (0.99932, 0.99946, s);
  float limb = 0.62 + 0.38 * smoothstep (0.99940, 0.99995, s);
  vec3 sunC = vec3 (1.0, 0.36, 0.10) * uSunInt * limb;
  col = mix (col, sunC, disc * (1.0 - uStorm * 0.55));
  if (dir.y > 0.03) {
    vec3 q = dir * 160.0; vec3 fq = floor (q); ivec3 c = ivec3 (fq);
    float r = hcell (c.xy + c.z * 37, 21u);
    vec3 ctr = fq + 0.5 + 0.4 * (vec3 (hcell (c.xy, 22u), hcell (c.yz, 23u), hcell (c.xz, 24u)) - 0.5);
    float d = length (q - ctr);
    float star = smoothstep (0.08, 0.0, d) * step (r, 0.045) * smoothstep (0.03, 0.25, dir.y);
    col += vec3 (0.8, 0.85, 1.0) * star * 0.9 * (1.0 - uStorm);
  }
  if (dir.y < 0.0) col = mix (uFogCol, hor * 0.85, exp (dir.y * 60.0));
  col = mix (col, uFogCol * 1.3, uStorm * 0.35 * (1.0 - t));
  fragColor = vec4 (finish (col, gl_FragCoord.xy), 1.0);
}
