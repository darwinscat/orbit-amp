// From the prototype's io-shaders.js (FS_GROUND), its body as it is but for its two discards, each under a switch the C++
// writes (IoGl.cpp): the inner level's hole under HOLE, for the outer levels only (io-gl.js drops the line from the near
// level's program), the far cut under FARCUT, for the outermost level only (no fragment of the inner three lies that far:
// their squares end within 1.3 km of the eye, the cut is 3.4 km). A shader that can discard is shaded the slow way on a
// tiling GPU; what is drawn is the same.
#include <hole>
#include "io/ftex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

uniform sampler2D uDetail, uCells;
uniform vec2 uDetOff0, uDetOff1, uDetOff2;
uniform vec4 uDetRot;                        // the first two detail layers' turns (cos, sin; cos, sin): no grid to see
uniform float uFarCut, uAct, uLakeT;            // uAct: the volcanoes' activity (io-world.js ACT, the old BLEND's scale)
uniform vec2 uCrackOff;                      // uCamCell modulo the breakouts' period (256 cells of 3.2 m)
uniform vec4 uHole; uniform float uHoleOn;
in vec3 vPos; in vec3 vN; in vec4 vA; in vec4 vB; in vec4 vC; in float vD;
out vec4 fragColor;
// a detail layer, turned: its value and its slope (turned back into the world)
vec3 detailAt (vec2 off, vec2 p, float sc, vec2 cs) {
  vec2 uv = off + vec2 (cs.x * p.x - cs.y * p.y, cs.y * p.x + cs.x * p.y) / sc;
  vec3 t = texture (uDetail, uv).rgb * 2.0 - 1.0;
  return vec3 (cs.x * t.x + cs.y * t.y, -cs.y * t.x + cs.x * t.y, t.z);
}
// Cellular noise from the cells' texture: F2 - F1 (zero on the borders: the cracks between the crust's plates) and the
// nearest cell's hash.
float worley (vec2 p, ivec2 shift, out float id) {
  vec2 n = floor (p), f = p - n;
  ivec2 ni = ivec2 (n) + shift;
  float d1 = 8.0, d2 = 8.0, h1 = 0.0;
  for (int j = -1; j <= 1; ++j)
    for (int i = -1; i <= 1; ++i) {
      vec4 t = texelFetch (uCells, (ni + ivec2 (i, j)) & 255, 0);
      vec2 r = vec2 (float (i), float (j)) + t.rg - f;
      float d = dot (r, r);
      if (d < d1) { d2 = d1; d1 = d; h1 = t.b; } else if (d < d2) { d2 = d; }
    }
  id = h1;
  return sqrt (d2) - sqrt (d1);
}
vec3 lavaColour (float t) {     // the lava's glow by its heat (0 dull red .. 1 yellow-white)
  return mix (mix (vec3 (0.55, 0.06, 0.01), vec3 (1.0, 0.32, 0.04), smoothstep (0.0, 0.45, t)), vec3 (1.0, 0.78, 0.42), smoothstep (0.45, 1.0, t));
}
// IO'S OWN GLOWS, everywhere on the plains (the owner, 19.11): small hot spots, short fissures, small lava ponds, glowing
// pit rims — sparse and scattered, each in a dark crusted patch of its own (not a network of veins). One at most per
// GLOW_CELL m cell: the cell's hash says whether (more of them as the volcanoes wake), what, where, how big, how hot (dull red to
// yellow-white), whether it breathes. Emissive, always there: by day they barely show, in the dark (night or eclipse)
// they carry the view; the volcanoes' activity (the hits) makes them hotter and more of them. Seen from 2.6 m the ground's shapes go flat (a 3 m pond 100 m off is a line a
// pixel tall, and far off a pixel row holds hundreds of metres of ground), so each also glows round its middle in screen
// space as the eye sees a hot point: a soft round halo a few pixels wide near, a point a pixel or two wide far, never
// fainter than a point should be (GLOW_POINT); fewer of them toward the horizon. The bigger ones warm the ground round
// them a little. One cell, a handful of hashes, no texture.
const float GLOW_CELL = 38.0;                  // one at most per cell (m); 40 % of the cells asleep, 75 % at full activity
const float GLOW_K = 1.6;                      // their light in the dark (night or eclipse: what carries the view there)
const float GLOW_DAY = 0.25;                   // their share by day (the eye set to the sunlit plain: a faint tint)
const float GLOW_POINT = 0.30;                 // the halo's least strength (a far point), up to 0.9 near
vec3 glowAt (vec2 pw, mat2 Jpx, float fp, float latFp, float awake, out float crust, out vec3 warm) {
  crust = 0.0; warm = vec3 (0.0);
  ivec2 c = ivec2 (floor (pw / GLOW_CELL));
  if (hcell (c, 811u) > 0.40 + 0.35 * uAct) return vec3 (0.0);
  float hx = hcell (c, 812u), hz = hcell (c, 813u), hk = hcell (c, 814u), hs = hcell (c, 815u);
  vec2 q = pw - (vec2 (c) + 0.22 + 0.56 * vec2 (hx, hz)) * GLOW_CELL;
  float len = length (q), sdf, size, ext, area, kindK;
  // (perf pass 20.11) most of a cell's pixels are far from its glow: past the crust's ragged edge (ext * 1.5 + 1 + 0.8)
  // and the shape's reach (ext, half a pixel's footprint more; or the shape under half a pixel, nearK 0) both are exactly
  // 0, and past ~6 halo radii the halo is under 1e-17 (e^-40: below what the scene's frame can hold, so no pixel
  // changes); only the warm round a pond or a pit is worked out there — by the same formulas
  float size0 = hk < 0.42 ? mix (0.5, 1.8, hs) : hk < 0.72 ? mix (0.10, 0.30, hs) : hk < 0.88 ? mix (1.5, 4.5, hs) : mix (1.5, 3.5, hs);
  float ext0 = hk < 0.42 ? size0 * 3.5 + 1.5 : hk < 0.72 ? 0.5 * mix (4.0, 13.0, hs) + 2.0 : hk < 0.88 ? size0 + 2.5 : size0 + 2.0;
  vec2 o = Jpx * q;                                          // the middle's offset in pixels
  float across = size0 / max (latFp, 1e-3);                  // how many pixels it spans across
  float R = clamp (1.2 + 0.5 * across, 1.2, 10.0);
  bool far = len > ext0 * 1.5 + 1.8 && (size0 < 0.5 * fp || len > ext0 + 0.5 * fp) && dot (o, o) > 40.0 * R * R;
  if (far && hk < 0.72) return vec3 (0.0);
  float ht = hcell (c, 816u), ha = hcell (c, 817u);
  if (far) {
    sdf = hk < 0.88 ? len - size0 : abs (len - size0) - 0.22 * (0.6 + hs);
    float T = clamp (0.15 + ht * (0.5 + 0.35 * awake) + 0.2 * awake, 0.0, 1.0);
    float breathe = ha > 0.55 ? 0.72 + 0.28 * sin (uTime * (0.35 + 0.6 * hx) + hz * 40.0) : 1.0;
    float I = (0.4 + 0.6 * awake) * mix (0.6, 1.4, ht) * (hk < 0.88 ? 0.8 : 0.95) * breathe * GLOW_K * mix (GLOW_DAY, 1.0, uDark);
    warm = lavaColour (T) * I * 0.10 * exp (-max (sdf, 0.0) / (1.0 + 1.2 * size0));
    return vec3 (0.0);
  }
  float heatProf = 1.0;
  if (hk < 0.42) {                       // a hot spot (a vent's mouth, a skylight's glow)
    size = mix (0.5, 1.8, hs); sdf = len - size; ext = size * 3.5 + 1.5; area = 3.1416 * size * size; kindK = 1.25;
    heatProf = 0.55 + 0.45 * smoothstep (size, 0.0, len);
  } else if (hk < 0.72) {                // a short fissure, a little crooked, thinning to its ends
    float L = mix (4.0, 13.0, hs), a = ha * 3.1416;
    vec2 dv = vec2 (cos (a), sin (a));
    float along = dot (q, dv), across = dot (q, vec2 (-dv.y, dv.x)) - 0.45 * sin (along * 0.8 + ha * 13.0) * (0.4 + 0.6 * hs);
    float tt = clamp (along / (0.5 * L), -1.0, 1.0);
    size = mix (0.10, 0.30, hs); sdf = max (abs (across) - size * sqrt (1.0 - tt * tt) - 0.02, abs (along) - 0.5 * L);
    ext = 0.5 * L + 2.0; area = L * size * 1.4; kindK = 1.0;
  } else if (hk < 0.88) {                // a small lava pond under a thin crust, its shore hottest
    size = mix (1.5, 4.5, hs); sdf = len - size; ext = size + 2.5; area = 3.1416 * size * size; kindK = 0.8;
    heatProf = (0.40 + 0.60 * exp (-max (size - len, 0.0) / 0.6)) * (0.75 + 0.25 * sin (q.x * 2.1 + ht * 9.0) * sin (q.y * 1.7 - ht * 5.0));
  } else {                               // a pit's rim glowing round a dark hole
    size = mix (1.5, 3.5, hs); float w = 0.22 * (0.6 + hs); sdf = abs (len - size) - w; ext = size + 2.0; area = 6.2832 * size * 2.0 * w; kindK = 0.95;
    crust = max (crust, 1.0 - smoothstep (size - 0.4, size, len));
  }
  // the dark crust round it, its edge ragged
  crust = max (crust, 1.0 - smoothstep (ext, ext * 1.5 + 1.0, len + 0.8 * sin (atan (q.y, q.x) * 5.0 + ha * 20.0)));
  float T = clamp (0.15 + ht * (0.5 + 0.35 * awake) + 0.2 * awake, 0.0, 1.0);
  float breathe = ha > 0.55 ? 0.72 + 0.28 * sin (uTime * (0.35 + 0.6 * hx) + hz * 40.0) : 1.0;
  float I = (0.4 + 0.6 * awake) * mix (0.6, 1.4, ht) * kindK * breathe * GLOW_K * mix (GLOW_DAY, 1.0, uDark);
  vec3 col = lavaColour (T);
  // the shape itself where the pixel is smaller than it (a pixel's edge), and round its middle the halo in screen space:
  // as wide as the shape looks across (a pixel at least, ten at most), its light the shape's spread over it far off
  float nearK = smoothstep (0.5, 1.5, size / max (fp, 1e-3));
  float shape = nearK > 0.0 ? 2.0 * clamp (0.5 - sdf / max (fp, 1e-3), 0.0, 1.0) * heatProf * nearK : 0.0;
  float dist = length (pw - uCamCell - uCamRel.xz);
  float keep = step (hcell (c, 818u), mix (1.0, 0.25, smoothstep (250.0, 2500.0, dist)));   // fewer toward the horizon
  float halo = exp (-dot (o, o) / (R * R)) * clamp (GLOW_POINT + 0.2 * across, GLOW_POINT, 0.9) * keep;
  // the ground round the bigger ones takes a little of their light
  if (hk >= 0.72) warm = col * I * 0.10 * exp (-max (sdf, 0.0) / (1.0 + 1.2 * size));
  return col * I * (shape + halo);
}
void main() {
#ifdef HOLE
  if (uHoleOn > 0.5 && vPos.x > uHole.x && vPos.z > uHole.y && vPos.x < uHole.z && vPos.z < uHole.w) discard;
#endif
  vec3 rel = vPos - uCamRel; float dist = length (rel);
#ifdef FARCUT
  if (dist > uFarCut) discard;
#endif
  // the world's xz under the pixel and its footprint (for the glows: world -> pixels, a pixel's size in metres)
  vec2 pw = vPos.xz + uCamCell, gdx = dFdx (vPos.xz), gdy = dFdy (vPos.xz);
  float gdet = gdx.x * gdy.y - gdx.y * gdy.x;
  mat2 Jpx = abs (gdet) > 1e-9 ? inverse (mat2 (gdx, gdy)) : mat2 (0.0);
  float fp = sqrt (abs (gdet)) + 1e-4, latFp = length (gdx) + 1e-4;
  vec3 V = rel / dist;
  vec3 n = normalize (vN);
  float df1 = exp (-dist / 90.0), df2 = exp (-dist / 18.0);
  vec3 d1 = detailAt (uDetOff0, vPos.xz, 4.3, uDetRot.xy);
  vec3 d2 = dist < 30.0 ? detailAt (uDetOff1, vPos.xz, 1.13, uDetRot.zw) : vec3 (0.0);
  float ad = abs (vD);
  float roadK = 1.0 - smoothstep (ROAD_HALF - 0.2, ROAD_HALF + 0.6, ad);
  vec4 f = featAt (vPos.xz);
  float flowD = (f.r - 0.5) * 48.0, fresh = f.g;
  float slope = 1.0 - n.y;

  // THE PLAINS' COLOURS: sulphur yellow, pale cream, ochre and orange by region; white SO2 frost in fields and rings;
  // red where short-chain sulphur fell fresh (Pele's ring, round the hot lakes); dark grey-green old silicate.
  float r1 = vA.a, r2 = vB.a, r3 = vC.a;
  // the regional units (kilometres): the sulphur's own saturated yellow, pale cream, tan and ochre, grey-green
  float u1 = r1 + 0.22 * (r2 - 0.5) + 0.07 * (r3 - 0.5) + 0.03 * d1.z * df1;
  vec3 alb = vec3 (0.88, 0.73, 0.22);
  alb = mix (alb, vec3 (0.86, 0.80, 0.56), smoothstep (0.60, 0.63, u1) * 0.85);
  alb = mix (alb, vec3 (0.70, 0.54, 0.30), 1.0 - smoothstep (0.36, 0.39, u1));
  alb = mix (alb, vec3 (0.52, 0.52, 0.38), 1.0 - smoothstep (0.24, 0.27, u1));
  // the patches near (tens of metres to a hundred and more, hard-edged and ragged): a deeper gold-orange, orange-red,
  // white SO2 frost, dark silicate spots — everywhere, so the mosaic reads by the road as it does from orbit
  float e = (d1.z * df1 + d2.z * df2) * 0.02;
  float plainK = 1.0 - smoothstep (0.18, 0.35, slope);            // on the plains, not down the cliffs
  float pD = vC.b * 0.6 + r2 * 0.4 - e, pA = vB.b * 0.55 + r3 * 0.45 + e, pB = r2 * 0.5 + vC.g * 0.5 - e, pC = vC.r * 0.6 + vB.r * 0.4 + e;
  alb = mix (alb, vec3 (0.92, 0.58, 0.14), smoothstep (0.635, 0.645, pD) * plainK);
  alb = mix (alb, vec3 (0.74, 0.34, 0.14), smoothstep (0.680, 0.690, pA) * plainK);
  float frost = max (f.a, smoothstep (0.672, 0.682, pB) * plainK);
  frost = max (frost, smoothstep (0.73, 0.77, r2 * 0.55 + r3 * 0.30 + vA.b * 0.15));
  alb = mix (alb, vec3 (0.93, 0.93, 0.89), frost * 0.92);
  alb = mix (alb, vec3 (0.20, 0.19, 0.15), smoothstep (0.735, 0.745, pC) * 0.9 * plainK * smoothstep (ROAD_HALF + 0.5, ROAD_HALF + 2.5, ad));
  alb = mix (alb, vec3 (0.64, 0.30, 0.15), f.b * 0.62);
  alb *= 0.88 + 0.24 * vC.g;
  // the layered cliffs: the strata in bands down their faces, white sapping frost at their feet
  float cliff = smoothstep (0.35, 0.65, slope) * (1.0 - roadK);
  if (cliff > 0.0) {
    float st = vPos.y / 4.3 + (vB.b - 0.5) * 3.0 + (vC.r - 0.5) * 0.8;
    float band = fract (st), layer = fract (sin (floor (st) * 17.31) * 43.7);
    vec3 strata = mix (vec3 (0.62, 0.52, 0.32), vec3 (0.80, 0.75, 0.58), smoothstep (0.1, 0.9, band) * 0.6 + 0.4 * layer) * (0.85 + 0.25 * layer);
    alb = mix (alb, strata, cliff);
  }
  alb = mix (alb, vec3 (0.90, 0.90, 0.88), smoothstep (0.18, 0.30, slope) * (1.0 - smoothstep (0.30, 0.45, slope)) * 0.5 * (1.0 - roadK));

  // THE FLOWS: fresh black glassy basalt, their margins and breakouts glowing when the volcanoes are awake; the old ones
  // dark grey under a dusting
  float flowIn = 1.0 - smoothstep (-0.6, 0.6, flowD + (d1.z - 0.5) * 3.0 * df1 + (vB.b - 0.5) * 6.0);
  vec3 emis = vec3 (0.0);
  if (flowIn > 0.0) {
    vec3 black = mix (vec3 (0.30, 0.29, 0.24), vec3 (0.035, 0.034, 0.036), smoothstep (0.15, 0.6, fresh));
    alb = mix (alb, black, flowIn);
  }
  float awake = smoothstep (0.15, 0.9, uAct);
  if (fresh > 0.4 && flowD < 3.0 && flowD > -30.0) {
    float edge = exp (-sq ((flowD + 0.6) / 1.1)) * smoothstep (0.4, 0.9, fresh);
    float idB; float eB = worley ((vPos.xz + uCrackOff) / 3.2, ivec2 (37, 91), idB);
    float crack = (1.0 - smoothstep (0.0, 0.08 + 0.06 * awake, eB)) * step (0.55 - 0.3 * awake, idB) * flowIn * smoothstep (-25.0, -2.0, flowD);
    float heat = awake * (0.55 + 0.45 * sin (uTime * 0.21 + idB * 31.0));
    emis += lavaColour (0.35 + 0.4 * heat) * (edge * (0.25 + 2.6 * awake) + crack * 1.6 * heat) * (0.25 + 0.75 * awake) * fresh;
  }

  // THE TRACK: graded, finer and a little darker, the rover's tracks worn into it (two pairs of tread bands, their
  // grousers every 0.3 m)
  if (roadK > 0.0) {
    vec3 bed = mix (alb, vec3 (0.62, 0.56, 0.36), 0.55) * (0.92 + 0.12 * d2.z);
    float tr = smoothstep (0.30, 0.0, abs (abs (ad - 1.9) - 0.75));
    vec2 lq = vPos.xz - uLoopC;
    float along = atan (lq.y, lq.x) * 760.0;
    float grouser = 0.5 + 0.5 * sin (along * TAU / 0.3);
    float aw = fwidth (along) / 0.3;
    grouser = mix (grouser, 0.5, smoothstep (0.3, 0.8, aw));
    bed *= 1.0 - tr * (0.08 + 0.06 * grouser);
    alb = mix (alb, bed, roadK);
    n = normalize (mix (n, vec3 (0.0, 1.0, 0.0), roadK * 0.7));
  }

  // THE PATERAE: dark floors (black-green silicate), the lava lake under its crust; the walls dark and lit from below
  float lakeGlow = 0.0, outPatera = 1.0;
  int ci = calderaAt (vPos.xz);
  if (ci >= 0) {
    int i = ci;
    vec4 A = uCaldA[i], B = uCaldB[i], C = uCaldC[i];
    float d = length (vPos.xz - A.xy);
    outPatera = smoothstep (A.z + A.w + 4.0, A.z + A.w + 12.0, d);
    if (d < A.z + A.w + 12.0) {
    float floorK = 1.0 - smoothstep (A.z - 2.0, A.z + A.w * 0.6, d);
    alb = mix (alb, vec3 (0.075, 0.080, 0.062) * (0.8 + 0.4 * vC.g), floorK);
    float wallK = smoothstep (A.z, A.z + A.w * 0.3, d) * (1.0 - smoothstep (A.z + A.w * 0.8, A.z + A.w + 8.0, d));
    alb = mix (alb, vec3 (0.30, 0.25, 0.16), wallK * 0.7);
    if (C.x > 0.0 && d < C.x + 1.0) {
      // the lake: plates of crust drifting slowly round, the cracks between them glowing, open lava where it is awake,
      // the shore's ring hottest (where the crust founders against the wall)
      float act = C.y;
      vec2 q = vPos.xz - A.xy;
      float ang = uLakeT * (0.0011 + 0.0006 * float (i));
      q = mat2 (cos (ang), sin (ang), -sin (ang), cos (ang)) * q;
      float cellM = 5.5 + 2.0 * fract (float (i) * 0.37);
      float id; float e = worley (q / cellM + vec2 (float (i) * 17.0, 3.0), ivec2 (0), id);
      float aa = max (fwidth (e), 1e-4);
      float w = mix (0.035, 0.20, act) * (0.6 + 0.8 * id);
      float crackK = (1.0 - smoothstep (w, w + aa * 1.5, e)) * min (1.0, w / max (aa, 1e-4) * 0.7 + 0.15);
      float open = smoothstep (0.85 - 0.55 * act, 0.95 - 0.5 * act, id) * smoothstep (0.15, 0.6, act);
      float shore = exp (-max (C.x - d, 0.0) / (2.0 + 4.0 * act));
      float inLake = 1.0 - smoothstep (C.x - 0.5, C.x + 0.5, d);
      float flick = 0.85 + 0.15 * sin (uTime * 1.3 + id * 40.0);
      float heat = clamp (crackK * (0.30 + 0.7 * act) + open * 0.9 + shore * (0.5 + 0.5 * act), 0.0, 1.0);
      vec3 lv = lavaColour (0.25 + 0.65 * act * (0.6 + 0.4 * heat)) * flick;
      emis += lv * heat * (0.35 + 2.4 * act) * inLake;
      alb = mix (alb, vec3 (0.045, 0.035, 0.032) + vec3 (0.05, 0.012, 0.0) * smoothstep (0.4, 0.0, e), inLake);   // the crust, warm near the cracks
      lakeGlow = max (lakeGlow, inLake * act);
    }
    }
  }

  // Io's own glows (glowAt): off the track and its shoulders, the cliffs and the paterae
  {
    float gk = smoothstep (ROAD_HALF + 2.0, ROAD_HALF + 6.0, ad) * plainK * outPatera;
    if (gk > 0.0) {
      float crust; vec3 warm;
      vec3 g = glowAt (pw, Jpx, fp, latFp, awake, crust, warm);
      alb = mix (alb, vec3 (0.085, 0.075, 0.065) * (0.8 + 0.4 * vC.g), crust * 0.85 * gk);
      emis += (g + alb * warm) * gk;
    }
  }
  // the detail on the normal: grit and frost grain near, finer still close by
  n = normalize (n + vec3 (d1.x, 0.0, d1.y) * 0.30 * df1 * (1.0 - roadK * 0.6) + vec3 (d2.x, 0.0, d2.y) * 0.16 * df2);
  alb *= 1.0 + 0.18 * d1.z * df1 + 0.10 * d2.z * df2;
  float sh = sunShadow (vPos, normalize (vN));
  vec3 col = lightAll (alb, n, vPos, sh);
  // the SO2 frost's sparkle in the Sun: a micro-facet from the fine grain, hashed per 12 cm so it twinkles as the rover moves
  // (both below are times the key's colour: at night it is exactly 0, and they are skipped)
  bool keyOn = uSunK > 0.01 && max (uSunCol.r, max (uSunCol.g, uSunCol.b)) > 0.0;
  if (dist < 70.0 && frost > 0.2 && keyOn) {
    vec3 d3 = texture (uDetail, uDetOff2 + vPos.xz / 0.13).rgb * 2.0 - 1.0;
    vec3 nm = normalize (n + vec3 (d3.x, 0.0, d3.y) * 1.6);
    vec3 Hh = normalize (uSun - V);
    float sp = pow (max (dot (nm, Hh), 0.0), 240.0);
    ivec2 gc = ivec2 (floor ((vPos.xz + uCamCell) * 8.0));
    float gate = step (0.96, hcell (gc, 71u));
    col += uSunCol * sp * gate * 2.0 * exp (-dist / 50.0) * frost * sh * uSunK;
  }
  // the fresh glass's sheen toward the Sun
  if (flowIn > 0.0 && max (uSunCol.r, max (uSunCol.g, uSunCol.b)) * uSunK > 0.0) {
    float sp = pow (max (dot (reflect (V, n), uSun), 0.0), 60.0);
    col += uSunCol * uSunK * sp * 0.35 * flowIn * smoothstep (0.3, 0.8, fresh) * sh;
  }
  col += emis * (1.0 + 0.8 * hitGlow () * lakeGlow);
  fragColor = vec4 (col, 1.0);
}
