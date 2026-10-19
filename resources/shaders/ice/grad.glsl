
uniform sampler2D uGrad1, uGrad2; uniform vec2 uGS1, uGS2;
#include "common/dss.glsl"
vec2 lineaFamGrad (vec2 pr, float warp, vec2 gwarp, vec2 d, vec2 pp, float P, float Lw, float sOff, float tOff, int base, uint seed, out float tint) {
  Linea o = lineaFam (pr, warp, d, pp, P, Lw, sOff, tOff, base, seed);
  vec2 dsdp = d + gwarp;
  float e1 = exp (-sq ((o.ds - o.w) / o.sg)), e2 = exp (-sq ((o.ds + o.w) / o.sg)), e3 = exp (-sq (o.ds / (0.7 * o.sg)));
  float de = e1 * (-2.0 * (o.ds - o.w) / sq (o.sg)) + e2 * (-2.0 * (o.ds + o.w) / sq (o.sg)) - 0.45 * e3 * (-2.0 * o.ds / sq (0.7 * o.sg));
  tint = o.amp * (0.45 * (e1 + e2) + e3);
  vec2 dAmp = dss (0.15, 0.55, o.x) * o.dAmp * pp;
  return o.H * (dAmp * (e1 + e2 - 0.45 * e3) + o.amp * de * dsdp);
}
vec2 lineaeGrad (vec2 pr, float bb, vec2 gBb, out float tint) {
  float warp = (bb - 0.5) * 160.0; vec2 gwarp = 160.0 * gBb; float t1, t2;
  vec2 g = lineaFamGrad (pr, warp, gwarp, vec2 (0.904752, 0.425939), vec2 (-0.425939, 0.904752), 1100.0, 1600.0, uLinOff.x, uLinOff.y, uLinBase.x, 101u, t1)
         + lineaFamGrad (pr, warp, gwarp, vec2 (-0.370181, 0.928960), vec2 (-0.928960, -0.370181), 1700.0, 2300.0, uLinOff.z, uLinOff.w, uLinBase.y, 202u, t2);
  tint = max (t1, t2);
  return g;
}
vec2 chaosGrad (vec2 pr, float mask, vec2 dmask, vec4 c, vec2 gCg, out float blockEdge, out float blockTop) {
  blockEdge = 0.0; blockTop = 0.0;
  if (mask < 0.005) return vec2 (0.0);
  vec2 q = uChaosOff + pr / CHB; vec2 fc = floor (q); vec2 f = q - fc;
  ivec2 cc = (uChaosBase + ivec2 (fc) + CHM * 64) % CHM;
  float ex = step (hcell (cc, 31u), 0.78);
  float hgt = 0.6 + 3.6 * sq (hcell (cc, 32u));
  vec2 tilt = (vec2 (hcell (cc, 33u), hcell (cc, 34u)) - 0.5) * 0.45;
  float m = 0.08 + 0.10 * hcell (cc, 35u);
  float mx = min (f.x, 1.0 - f.x), my = min (f.y, 1.0 - f.y);
  float sx = smoothstep (m, m + 0.16, mx), sy = smoothstep (m, m + 0.16, my);
  vec2 dpl = vec2 (dss (m, m + 0.16, mx) * (f.x < 0.5 ? 1.0 : -1.0) * sy, dss (m, m + 0.16, my) * (f.y < 0.5 ? 1.0 : -1.0) * sx) / CHB;
  float pl = sx * sy;
  float top = hgt + dot (tilt, (f - 0.5) * CHB);
  float h = ex * pl * top + (c.g - 0.5) * 1.2 - 0.8;
  blockEdge = mask * ex * (1.0 - pl) * step (0.02, pl);
  blockTop = mask * ex * pl;
  return mask * (ex * (dpl * top + pl * tilt) + 1.2 * gCg) + dmask * h;
}
vec2 structFootGrad (vec2 pr, out float shadow) {
  vec2 q = uStructOff + pr / SC; vec2 fc = floor (q);
  vec2 fcell; float rad; int v; float scl, ang; shadow = 0.0;
  if (! structCell (uStructBase + ivec2 (fc), fcell, rad, v, scl, ang)) return vec2 (0.0);
  vec2 footRel = (fc + fcell - uStructOff) * SC; vec2 dv = pr - footRel;
  float r2 = dot (dv, dv); float s2 = sq (rad + 1.5);
  float drift = 0.55 * scl * exp (-r2 / (2.0 * s2));
  shadow = 0.45 * exp (-r2 / (2.0 * sq (rad * 0.75)));
  return -drift * dv / s2;
}
vec2 terrainGrad (vec2 pr, vec4 a, vec4 b, vec4 c, out float lineaTint, out float chaosM, out float blockEdge, out float blockTop, out float shadow) {
  vec2 uv0 = uTexOff0 + pr / L0, uv1 = uTexOff1 + pr / L1, uv2 = uTexOff2 + pr / L2;
  vec4 s1 = vec4 (uGS1.x, uGS1.x, uGS1.y, uGS1.y), s2 = vec4 (uGS2.x, uGS2.x, uGS2.y, uGS2.y);
  vec4 g1a = (texture (uGrad1, uv0) * 2.0 - 1.0) * s1 / L0, g2a = (texture (uGrad2, uv0) * 2.0 - 1.0) * s2 / L0;
  vec4 g1b = (texture (uGrad1, uv1) * 2.0 - 1.0) * s1 / L1, g2b = (texture (uGrad2, uv1) * 2.0 - 1.0) * s2 / L1;
  vec4 g1c = (texture (uGrad1, uv2) * 2.0 - 1.0) * s1 / L2, g2c = (texture (uGrad2, uv2) * 2.0 - 1.0) * s2 / L2;
  vec2 gAr = g1a.xy, gAb = g1a.zw, gAg = g2a.xy, gAa = g2a.zw;
  vec2 gBr = g1b.xy, gBb = g1b.zw, gBa = g2b.zw;
  vec2 gCr = g1c.xy, gCb = g1c.zw, gCg = g2c.xy;
  vec2 rel = pr - uCamFrac; float dist = length (rel);
  vec2 g = 70.0 * gAa;
  g += 30.0 * (gBr * (0.3 + 0.7 * a.g) + (b.r - 0.5) * 0.7 * gAg);
  float up = smoothstep (0.55, 0.8, a.b);
  g += 24.0 * (gAr * up + (a.r - 0.5) * dss (0.55, 0.8, a.b) * gAb);
  float fade = 1.0 - smoothstep (600.0, 2000.0, dist);
  g += fade * (1.6 * gCr + 0.6 * gCb);
  float fade3 = 1.0 - smoothstep (250.0, 600.0, dist);
  if (fade3 > 0.0) { vec2 uv3 = uTexOff3 + pr / L3;
    vec4 g1d = (texture (uGrad1, uv3) * 2.0 - 1.0) * s1 / L3, g2d = (texture (uGrad2, uv3) * 2.0 - 1.0) * s2 / L3;
    g += fade3 * (1.1 * g1d.xy + 0.9 * g1d.zw + 0.4 * g2d.xy); }
  lineaTint = 0.0; blockEdge = 0.0; blockTop = 0.0; shadow = 0.0;
  float cfade = 1.0 - smoothstep (700.0, 900.0, dist);
  chaosM = chaosMaskOf (b) * cfade;
  g += lineaeGrad (pr, b.b, gBb, lineaTint);
  if (dist < 900.0) {
    vec2 dcm = dss (0.66, 0.74, b.a) * gBa * cfade - chaosMaskOf (b) * dss (700.0, 900.0, dist) * rel / max (dist, 1.0);
    g += chaosGrad (pr, chaosM, dcm, c, gCg, blockEdge, blockTop);
    if (dist < 400.0) { vec2 gs = structFootGrad (pr, shadow); float sf = 1.0 - smoothstep (300.0, 400.0, dist);
      g += gs * (1.0 - chaosM) * sf; shadow *= (1.0 - chaosM) * sf; } }
  g -= rel / CURV_R;
  return g;
}
