#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

uniform sampler2D uDetail;
uniform vec2 uDetOff0, uDetOff1;
in vec3 vPos; in vec3 vN; in vec4 vA, vB, vC;
out vec4 fragColor;
void main() {
  vec3 rel = vPos - uCamRel; float dist = length (rel); vec3 V = rel / dist;
  vec3 n = normalize (vN);
  vec3 d1 = texture (uDetail, uDetOff0 + vPos.xz / 2.6).rgb * 2.0 - 1.0;    // the ripples
  vec3 d2 = texture (uDetail, uDetOff1 + vPos.xz / 0.7).rgb * 2.0 - 1.0;    // the grit
  float df1 = exp (-dist / 38.0), df2 = exp (-dist / 11.0);
  n = normalize (n + vec3 (d1.x, 0.0, d1.y) * 0.55 * df1 + vec3 (d2.x, 0.0, d2.y) * 0.30 * df2);
  float slope = 1.0 - n.y;
  vec3 sand = mix (vec3 (0.78, 0.68, 0.76), vec3 (0.84, 0.78, 0.70), vB.r);  // pale lilac, pale shell
  vec3 rock = vec3 (0.13, 0.11, 0.18);
  float rockM = clamp (smoothstep (0.22, 0.50, slope) + smoothstep (0.72, 0.92, vC.a) * 0.9, 0.0, 1.0);
  vec3 alb = mix (sand, rock, rockM);
  alb *= 1.0 + 0.14 * d1.z * df1 + 0.12 * d2.z * df2;
  vec3 col = shadeGround (alb, n, vPos, rel, dist);
  // The crust on the rock: a faint teal lichen in the hollows.
  col += vec3 (0.05, 0.22, 0.18) * rockM * (0.5 + 0.5 * d1.z) * 0.35;
  // Living sand: specks that glow and pulse, cyan, here and there.
  vec2 cellP = vPos.xz * 2.5; ivec2 ci = ivec2 (floor (cellP + uCamCell * 2.5));
  float hsp = hcell (ci, 77u);
  float hk = hitK (dist);
  if (hsp > 0.975 - 0.017 * (hk - 1.0)) {                 // an attack wakes more of them
    vec2 off = vec2 (hcell (ci, 78u), hcell (ci, 79u));
    float r = length (fract (cellP) - off);
    float glow = pow (max (0.0, 1.0 - r * 7.0), 2.0) * (0.55 + 0.45 * sin (uTime * 1.7 + hsp * 200.0)) * exp (-dist / 22.0);
    col += vec3 (0.35, 1.0, 0.95) * glow * 1.6 * hk * (1.0 - rockM);
  }
  col = applyFog (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
