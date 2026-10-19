
uniform sampler2D uGrad1, uGrad2; uniform vec2 uGS1, uGS2;
#include "common/dss.glsl"
vec2 craterGrad (vec2 p, float mask) {
  vec2 q = p / CRC; vec2 fc = floor (q);
  ivec2 c = (ivec2 (fc) + CRM * 64) % CRM;
  float r0 = hcell (c, 1u);
  if (r0 > 0.42 || mask < 0.01) return vec2 (0.0);
  vec2 ctr = (fc + 0.25 + 0.5 * vec2 (hcell (c, 2u), hcell (c, 3u))) * CRC;
  float k = hcell (c, 4u);
  float R = CRC * (0.045 + 0.16 * k * k);
  vec2 dv = p - ctr; float L = max (length (dv), 1e-3); float d = L / R;
  if (d > 1.6) return vec2 (0.0);
  float r = (d - 1.0) / 0.22;          // squared, not pow (): pow of a negative is undefined in GLSL, NaN on some GPUs
  float rim = exp (-r * r);
  float dhdd = mask * R * (0.24 * 2.0 * d * step (d, 1.0) + 0.065 * rim * (-2.0 * (d - 1.0) / (0.22 * 0.22)));
  return dhdd * dv / (L * R);
}
vec2 terrainGrad (vec2 pr, vec4 a, vec4 b, vec4 c) {
  vec2 uv0 = uTexOff0 + pr / L0, uv1 = uTexOff1 + pr / L1, uv2 = uTexOff2 + pr / L2;
  vec4 s1 = vec4 (uGS1.x, uGS1.x, uGS1.y, uGS1.y), s2 = vec4 (uGS2.x, uGS2.x, uGS2.y, uGS2.y);
  vec4 g1a = (texture (uGrad1, uv0) * 2.0 - 1.0) * s1 / L0, g2a = (texture (uGrad2, uv0) * 2.0 - 1.0) * s2 / L0;
  vec4 g1b = (texture (uGrad1, uv1) * 2.0 - 1.0) * s1 / L1, g2b = (texture (uGrad2, uv1) * 2.0 - 1.0) * s2 / L1;
  vec4 g1c = (texture (uGrad1, uv2) * 2.0 - 1.0) * s1 / L2;
  vec2 gAr = g1a.xy, gAb = g1a.zw, gAg = g2a.xy, gAa = g2a.zw;
  vec2 gBr = g1b.xy, gBb = g1b.zw, gBg = g2b.xy;
  vec2 gCr = g1c.xy, gCb = g1c.zw;
  float dist = length (pr - uCamFrac);
  float mtnMask = smoothstep (0.50, 0.92, a.r); vec2 dM = dss (0.50, 0.92, a.r) * gAr;
  vec2 g = 430.0 * (1.25 * pow (mtnMask, 0.25) * dM * (0.5 + 0.9 * b.b) + pow (mtnMask, 1.25) * 0.9 * gBb);
  g += 90.0 * gAa;
  g += 48.0 * (gBr * (0.4 + 0.6 * a.g) + (b.r - 0.5) * 0.6 * gAg);
  float plainM = 1.0 - mtnMask; vec2 dPlain = -dM;
  float m1 = smoothstep (0.60, 0.64, b.g), m2 = smoothstep (0.35, 0.6, a.b);
  vec2 dm1 = dss (0.60, 0.64, b.g) * gBg, dm2 = dss (0.35, 0.6, a.b) * gAb;
  float mesaM = m1 * m2 * plainM;
  g += 24.0 * (1.0 + 0.35 * (c.g - 0.5)) * (dm1 * m2 * plainM + m1 * dm2 * plainM + m1 * m2 * dPlain);
  float ridge = 1.0 - abs (2.0 * a.g - 1.0); vec2 dRidge = -2.0 * sign (2.0 * a.g - 1.0) * gAg;
  float c3 = smoothstep (0.80, 0.97, ridge), c4 = smoothstep (0.45, 0.62, a.b);
  vec2 dc3 = dss (0.80, 0.97, ridge) * dRidge, dc4 = dss (0.45, 0.62, a.b) * gAb;
  g += -34.0 * (dc3 * plainM * c4 + c3 * dPlain * c4 + c3 * plainM * dc4);
  float fade = 1.0 - smoothstep (800.0, 2500.0, dist);
  g += fade * (5.0 * gCr + 1.3 * gCb);
  float crm = plainM * (1.0 - mesaM) * smoothstep (0.90, 0.75, ridge);
  g += craterGrad (pr + uCamCell, crm);
  return g;
}
