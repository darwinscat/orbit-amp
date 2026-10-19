#include "ice/height.glsl"
#include "ice/grad.glsl"
#include "ice/light.glsl"

uniform sampler2D uDetail;
uniform vec3 uCamRel;
uniform vec2 uDetOff0, uDetOff1, uDetOff2;
in vec3 vPos; in vec4 vA, vB, vC;
out vec4 fragColor;
void main() {
  vec3 rel = vPos - uCamRel; float dist = length (rel); vec3 V = rel / dist;
  float lineaTint, chaosM, blockEdge, blockTop, shadow;
  vec2 g = terrainGrad (vPos.xz, vA, vB, vC, lineaTint, chaosM, blockEdge, blockTop, shadow);
  vec3 n = normalize (vec3 (-g.x, 1.0, -g.y));
  vec3 d1 = texture (uDetail, uDetOff0 + vPos.xz / 3.1).rgb * 2.0 - 1.0;     // wind ripples, sastrugi
  vec3 d2 = texture (uDetail, uDetOff1 + vPos.xz / 0.45).rgb * 2.0 - 1.0;    // crust grain
  float df1 = exp (-dist / 160.0), df2 = exp (-dist / 35.0);
  vec3 nd = normalize (n + vec3 (d1.x, 0.0, d1.y) * 0.55 * df1 + vec3 (d2.x, 0.0, d2.y) * 0.16 * df2);
  float slope = 1.0 - nd.y;
  // The albedo: snow, blue ice on the steep faces and the block edges, the reddish-brown salts of the lineae.
  vec3 snow = vec3 (0.93, 0.95, 0.99);
  vec3 ice = vec3 (0.72, 0.84, 0.97);
  vec3 salt = vec3 (0.58, 0.40, 0.29);
  vec3 alb = mix (snow, ice, smoothstep (0.08, 0.35, slope));
  alb = mix (alb, ice, blockEdge * 0.8);
  float saltM = lineaTint * (0.55 + 0.45 * vB.g) * smoothstep (0.1, 0.5, lineaTint);
  alb = mix (alb, salt, clamp (saltM, 0.0, 0.7));
  alb = mix (alb, vec3 (0.80, 0.86, 0.95), 0.5 * chaosM * (1.0 - blockTop));   // the rubble between the blocks, greyer
  alb *= 1.0 + 0.07 * d1.z * df1 + 0.06 * d2.z * df2;
  alb *= 0.96 + 0.08 * vB.g;
  vec3 col = shadeSnow (alb, nd);
  // The sparkle: a micro-facet from the fine crust grain, hashed per 12 cm of world so it twinkles as the rover moves.
  if (dist < 160.0) {
    vec3 d3 = texture (uDetail, uDetOff2 + vPos.xz / 0.13).rgb * 2.0 - 1.0;
    vec3 nm = normalize (nd + vec3 (d3.x, 0.0, d3.y) * 1.6);
    vec3 H = normalize (uSun - V);
    float sp = pow (max (dot (nm, H), 0.0), 240.0);
    ivec2 gc = ivec2 (floor ((vPos.xz + uCamCell) * 8.0));
    float gate = step (0.955, hcell (gc, 71u));
    col += vec3 (1.0, 0.98, 0.95) * sp * gate * 2.2 * exp (-dist / 70.0) * max (dot (nd, uSun), 0.0) * uSunInt;
  }
  col = mix (col, col * vec3 (0.50, 0.62, 0.85), shadow);
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
