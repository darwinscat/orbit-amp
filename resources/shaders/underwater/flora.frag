#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

uniform float uFloraKind;
in vec3 vN; in vec3 vRel; in vec3 vPos; in vec3 vCol; in vec2 vUV; in float vEmis; in float vSeed;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  if (uFloraKind < 0.5) {                           // the fan: its outline, its scalloped top, its lace
    float hw = 0.5 * (0.22 + 0.78 * sqrt (vUV.y));
    if (abs (vUV.x - 0.5) > hw) discard;
    if (vUV.y > 0.93 + 0.07 * sin (vUV.x * 60.0 + vSeed * 10.0)) discard;
    // A sea fan: veins radiating from the foot, rings across them; open between.
    vec2 q = vec2 (vUV.x - 0.5, vUV.y + 0.08);
    float ang = atan (q.x, q.y), rad = length (q);
    float veins = abs (sin (ang * 11.0 + vSeed * 4.0));
    float rings = abs (sin (rad * 34.0 + vSeed * 6.0));
    float web = min (veins, rings);
    if (web > 0.42 && vUV.y > 0.08) discard;
  }
  if (dot (n, V) > 0.0) n = -n;                      // sheets and ribbons: lit from the side we see
  vec3 nl = normalize (mix (n, vec3 (0.0, 1.0, 0.0), 0.55));   // thin and translucent: the light from above gets through
  vec3 alb = vCol;
  if (uFloraKind < 0.5) alb = mix (alb, alb * 1.6 + vec3 (0.15, 0.05, 0.1), vUV.y * vUV.y);              // the fan pales to its edge
  if (uFloraKind > 2.5 && uFloraKind < 3.5) alb = mix (alb, vec3 (0.95, 0.65, 0.25), smoothstep (0.38, 0.5, abs (vUV.x - 0.5)));   // the kelp's amber edges
  vec3 col = shadeGround (alb, nl, vPos, vRel, dist) + alb * uFogLevel * 0.45;
  float rim = pow (max (1.0 - abs (dot (n, V)), 0.0), 2.0);
  col += alb * rim * 0.5;
  float pulse = 0.6 + 0.4 * sin (uTime * 1.7 + vSeed * 50.0 + vUV.y * 4.0);
  col += vCol * vEmis * (1.2 + 1.0 * pulse) * exp (-dist / 35.0) * hitK (dist);
  col = applyFog (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
