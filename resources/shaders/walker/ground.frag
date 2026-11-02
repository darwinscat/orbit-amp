#include "walker/common.glsl"
#include "walker/light.glsl"

uniform sampler2D uDetail;
uniform vec4 uPrint[12];            // x, z (absolute), t0, radius — where the feet have fallen
in vec3 vRel;
out vec4 fragColor;
void main () {
  vec3 toP = vRel - vec3 (0.0, uCamY, 0.0); float dist = length (toP); vec3 V = toP / dist;
  vec2 p = uCamXZ + vRel.xz;
  float e = HP / HTEX;
  float hx = texture (uHeight, (p + vec2 (e, 0.0)) / HP).r - texture (uHeight, (p - vec2 (e, 0.0)) / HP).r;
  float hz = texture (uHeight, (p + vec2 (0.0, e)) / HP).r - texture (uHeight, (p - vec2 (0.0, e)) / HP).r;
  vec3 n = normalize (vec3 (-hx / (2.0 * e), 1.0, -hz / (2.0 * e)));
  vec3 d1 = texture (uDetail, p / 2.3).rgb * 2.0 - 1.0;
  vec3 d2 = texture (uDetail, p / 0.41).rgb * 2.0 - 1.0;
  float f1 = exp (-dist / 70.0), f2 = exp (-dist / 20.0);
  n = normalize (n + vec3 (d1.x, 0.0, d1.y) * 0.6 * f1 + vec3 (d2.x, 0.0, d2.y) * 0.45 * f2);
  vec4 g = texture (uHeight, p / HP);
  vec3 alb = mix (vec3 (0.17, 0.10, 0.15), vec3 (0.10, 0.16, 0.13), smoothstep (0.35, 0.7, g.b));
  alb *= 0.75 + 0.45 * d1.z * f1 + 0.35 * d2.z * f2;
  // The footprints: the moss pressed flat and dark, a pale rim of crushed caps.
  float press = 0.0;
  for (int i = 0; i < 12; ++i) {
    float d = length (p - uPrint[i].xy);
    float age = uTime - uPrint[i].z;
    if (age < 0.0 || age > 60.0) continue;
    press = max (press, smoothstep (uPrint[i].w, uPrint[i].w * 0.6, d) * smoothstep (0.0, 0.15, age) * (1.0 - age / 60.0));
  }
  alb *= 1.0 - 0.55 * press;
  vec3 lm = lightAt (vRel.xz);
  float sheen = 0.3 * pow (1.0 - max (dot (n, -V), 0.0), 3.0) * f1;
  vec3 col = alb * (lm * (3.2 * (0.5 + 0.5 * n.y) + sheen * 6.0) + ambient (n) * 2.6 + sunLight (n) * 1.5);
  col = applyFog (col, dist, V, lm);
  fragColor = vec4 (finish (col, gl_FragCoord.xy), 1.0);
}
