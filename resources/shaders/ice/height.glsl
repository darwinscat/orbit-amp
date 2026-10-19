
uniform sampler2D uHeight;
uniform vec2 uCamCell;      // the integer world xz the frame is relative to (exact in float)
uniform vec2 uCamFrac;      // the camera's xz minus uCamCell
uniform vec2 uTexOff0, uTexOff1, uTexOff2, uTexOff3;  // fract (uCamCell / L_i) (+ the fixed layer offsets), from doubles
uniform vec4 uLinOff;       // (sOffA, tOffA, sOffB, tOffB): the lineae coordinates of uCamCell, wrapped, metres
uniform ivec2 uLinBase;     // the line index of uCamCell per family
uniform vec2 uChaosOff; uniform ivec2 uChaosBase;   // fract / floor of uCamCell / the chaos cell
uniform vec2 uStructOff; uniform ivec2 uStructBase; // fract / floor of uCamCell / the structure cell
const float L0 = 7000.0, L1 = 1250.0, L2 = 190.0, L3 = 50.0;
const float CHB = 14.0; const int CHM = 47500;      // the chaos cell, WORLD / CHB
const float SC = 70.0; const int SM = 9500;         // the structure cell, WORLD / SC
const float PROB_IN = 0.3, PROB_OUT = 0.035;
const float CURV_R = 1560000.0;                     // the moon's radius: the ground falls away as dist^2 / 2R
#include "common/ih.glsl"
#include "common/hcell.glsl"
#include "common/sq.glsl"

// The lineae: two families of long, gently bent double ridges, each line with its own height and width,
// fading in and out along its length. sv = the signed coordinate across the lines (m), tv = along (m).
struct Linea { float h; float ds; float w; float sg; float amp; float dAmp; float H; float x; };   // for H and its gradient
Linea lineaFam (vec2 pr, float warp, vec2 d, vec2 pp, float P, float Lw, float sOff, float tOff, int base, uint seed) {
  Linea o;
  float sv = sOff + dot (pr, d) + warp;
  float fi = floor (sv / P); int i = base + int (fi);
  o.ds = sv - (fi + 0.5) * P;
  float r1 = hcell (ivec2 (i, 7), seed), r2 = hcell (ivec2 (i, 8), seed), r3 = hcell (ivec2 (i, 9), seed);
  o.H = 3.0 + 8.0 * r1 * r1; o.w = 9.0 + 9.0 * r2; o.sg = 0.55 * o.w;
  float tv = tOff + dot (pr, pp);
  float ph = tv / Lw * 6.2831853 + r3 * 6.2831853;
  o.x = 0.5 + 0.5 * sin (ph);
  o.amp = smoothstep (0.15, 0.55, o.x);
  o.dAmp = 0.5 * cos (ph) * (6.2831853 / Lw);     // d(x)/d(tv); times dss (0.15, 0.55, x) outside
  float e1 = exp (-sq ((o.ds - o.w) / o.sg)), e2 = exp (-sq ((o.ds + o.w) / o.sg)), e3 = exp (-sq (o.ds / (0.7 * o.sg)));
  o.h = o.amp * o.H * (e1 + e2 - 0.45 * e3);
  return o;
}
float lineae (vec2 pr, float bb) {
  float warp = (bb - 0.5) * 160.0;
  Linea A = lineaFam (pr, warp, vec2 (0.904752, 0.425939), vec2 (-0.425939, 0.904752), 1100.0, 1600.0, uLinOff.x, uLinOff.y, uLinBase.x, 101u);
  Linea B = lineaFam (pr, warp, vec2 (-0.370181, 0.928960), vec2 (-0.928960, -0.370181), 1700.0, 2300.0, uLinOff.z, uLinOff.w, uLinBase.y, 202u);
  return A.h + B.h;
}
// The chaos terrain: tilted ice blocks on a hashed 14 m grid, inside patches of the 1250 m layer.
float chaosMaskOf (vec4 b) { return smoothstep (0.66, 0.74, b.a); }
float chaos (vec2 pr, float mask, vec4 c) {
  if (mask < 0.005) return 0.0;
  vec2 q = uChaosOff + pr / CHB; vec2 fc = floor (q); vec2 f = q - fc;
  ivec2 cc = (uChaosBase + ivec2 (fc) + CHM * 64) % CHM;
  float ex = step (hcell (cc, 31u), 0.78);
  float hgt = 0.6 + 3.6 * sq (hcell (cc, 32u));
  vec2 tilt = (vec2 (hcell (cc, 33u), hcell (cc, 34u)) - 0.5) * 0.45;
  float m = 0.08 + 0.10 * hcell (cc, 35u);
  float mx = min (f.x, 1.0 - f.x), my = min (f.y, 1.0 - f.y);
  float pl = smoothstep (m, m + 0.16, mx) * smoothstep (m, m + 0.16, my);
  float top = hgt + dot (tilt, (f - 0.5) * CHB);
  return mask * (ex * pl * top + (c.g - 0.5) * 1.2 - 0.8);
}
// The steel: one structure per hashed 70 m cell, in groups (a 280 m super-cell decides dense or sparse).
bool structCell (ivec2 cij, out vec2 footCell, out float rad, out int variant, out float scl, out float ang) {
  ivec2 c = (cij + SM * 64) % SM;
  ivec2 g = (cij + SM * 64) / 4;
  float prob = (hcell (g, 47u) < 0.3) ? PROB_IN : PROB_OUT;
  float r0 = hcell (c, 41u);
  footCell = vec2 (0.0); rad = 0.0; variant = 0; scl = 1.0; ang = 0.0;
  if (r0 > prob) return false;
  variant = int (hcell (c, 42u) * 4.0);
  footCell = 0.22 + 0.56 * vec2 (hcell (c, 43u), hcell (c, 44u));
  scl = 0.8 + 0.5 * hcell (c, 45u); ang = 6.2831853 * hcell (c, 46u);
  rad = (variant == 0 ? 2.4 : variant == 1 ? 6.0 : variant == 2 ? 2.8 : 3.4) * scl;
  return true;
}
// The snow drift at a structure's foot (height) and its contact shadow (0..1), from the same lookup.
vec2 structFoot (vec2 pr) {
  vec2 q = uStructOff + pr / SC; vec2 fc = floor (q);
  vec2 fcell; float rad; int v; float scl, ang;
  if (! structCell (uStructBase + ivec2 (fc), fcell, rad, v, scl, ang)) return vec2 (0.0);
  vec2 footRel = (fc + fcell - uStructOff) * SC;
  float r2 = dot (pr - footRel, pr - footRel);
  float drift = 0.55 * scl * exp (-r2 / (2.0 * sq (rad + 1.5)));
  float shadow = 0.45 * exp (-r2 / (2.0 * sq (rad * 0.75)));
  return vec2 (drift, shadow);
}
float terrainH (vec2 pr, out vec4 a, out vec4 b, out vec4 c) {   // pr = world xz relative to uCamCell, metres
  a = HTEX (uTexOff0 + pr / L0);
  b = HTEX (uTexOff1 + pr / L1);
  c = HTEX (uTexOff2 + pr / L2);
  vec2 rel = pr - uCamFrac; float d2 = dot (rel, rel); float dist = sqrt (d2);
  float swell = (a.a - 0.5) * 70.0;
  float hills = (b.r - 0.5) * 30.0 * (0.3 + 0.7 * a.g);
  float ridges = (a.r - 0.5) * 24.0 * smoothstep (0.55, 0.8, a.b);      // broad snowy uplands here and there
  float fade = 1.0 - smoothstep (600.0, 2000.0, dist);
  float dune = ((c.r - 0.5) * 1.6 + (c.b - 0.5) * 0.6) * fade;
  float fade3 = 1.0 - smoothstep (250.0, 600.0, dist);
  if (fade3 > 0.0) { vec4 d = HTEX (uTexOff3 + pr / L3); dune += ((d.r - 0.5) * 1.1 + (d.b - 0.5) * 0.9 + (d.g - 0.5) * 0.4) * fade3; }
  float cm = chaosMaskOf (b) * (1.0 - smoothstep (700.0, 900.0, dist));
  float h = swell + hills + ridges + dune + lineae (pr, b.b) - d2 / (2.0 * CURV_R);
  if (dist < 900.0) h += chaos (pr, cm, c);
  if (dist < 400.0) h += structFoot (pr).x * (1.0 - cm) * (1.0 - smoothstep (300.0, 400.0, dist));
  return h;
}
float terrainH (vec2 pr) { vec4 a, b, c; return terrainH (pr, a, b, c); }
