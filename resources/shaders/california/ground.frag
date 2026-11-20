#include <level>
#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

uniform sampler2D uDetail;
uniform vec2 uDetOff0, uDetOff1;
uniform vec4 uHole; uniform float uHoleOn;   // the inner level's square (rel xz min, max): not drawn here
uniform float uFarCut;                       // the ground ends here (radially), faded into the first ridge
uniform float uDashes;                       // the centre line's dashes round the loop (a whole number)
uniform vec3 uFindC, uFindU, uFindV;         // the peace sign of flowers: its centre (rel), its axes in the slope
uniform vec4 uFind;                          // its radius, its strength (0 = none), its stretch up the slope (to read from the road)
uniform vec4 uFindSh, uFindSh1;             // the finds' shadows on the ground (the van's, the monolith's): from (rel xz), to
uniform vec4 uFindShW;                       // their half widths and darknesses
uniform float uDebug;                        // a developer's look: 1 the hills' shadow, 2 the shadows, 3 the normal
uniform sampler2D uChalk;                    // the chalk drawings' atlas (straight alpha, the colour and the wear in it)
uniform vec4 uChalkA[4], uChalkB[4], uChalkC[4];   // the four nearest: centre (rel xz), the way (xz); half across, half along, on; the cell
in vec3 vPos; in vec3 vN; in float vD; in float vSunV; in vec4 vF; in vec4 vHaze;
out vec4 fragColor;
void main() {
#ifndef NEAR
  if (uHoleOn > 0.5 && vPos.x > uHole.x && vPos.z > uHole.y && vPos.x < uHole.z && vPos.z < uHole.w) discard;
#endif
  vec3 rel = vPos - uCamRel; float dist = length (rel);
#ifndef NEAR
  if (dist > uFarCut) discard;
#endif
  vec3 V = rel / dist;
  vec3 n0 = normalize (vN);
  float ad = abs (vD);
  // The grass's strokes: the detail map (gradient in RG, value in B; A the gravel and the asphalt), a second, finer
  // scale near.
  float df1 = exp (-dist / 70.0);
  vec4 d1 = texture (uDetail, uDetOff0 + vPos.xz / 3.3);
  vec3 g1 = d1.rgb * 2.0 - 1.0;
  vec3 n = n0 + vec3 (g1.x, 0.0, g1.y) * 0.45 * df1;
  // Dry summer grass: gold to pale straw, olive in the hollows and under the oak groves.
  vec3 straw = mix (vec3 (0.82, 0.60, 0.27), vec3 (0.94, 0.77, 0.45), vF.w);
  vec3 alb = mix (straw, vec3 (0.50, 0.46, 0.20), vF.z) * (0.84 + 0.32 * mix (0.5, d1.b, df1));
  float transl = 0.6;
  // The flowers' carpets: near, the instanced flowers stand on grass with flower heads in it; far, the colour itself.
  float P = vF.x, Lu = vF.y;
  float farK = smoothstep (16.0, 46.0, dist);
  float covP = P * mix (0.30, 0.92, farK), covL = Lu * mix (0.12, 0.38, farK);
  vec3 poppy = vec3 (1.0, 0.47, 0.05);
  vec4 d2 = vec4 (0.5);
  float hsp = 0.5;
  if (dist < 48.0) {
    float df2 = exp (-dist / 16.0);
    d2 = texture (uDetail, uDetOff1 + vPos.xz / 0.85);
    vec3 g2 = d2.rgb * 2.0 - 1.0;
    n += vec3 (g2.x, 0.0, g2.y) * 0.40 * df2;
    alb *= 1.0 + 0.16 * (d2.b - 0.5) * df2;
    ivec2 ci = ivec2 (floor (vPos.xz * 4.0)) + ivec2 (uCamCell) * 4;
    hsp = hcell (ci, 61u);
    vec2 dc = fract (vPos.xz * 4.0) - 0.5 - 0.3 * vec2 (fract (hsp * 13.7) - 0.5, fract (hsp * 29.3) - 0.5);
    float rr = 0.07 + 0.10 * fract (hsp * 51.3);
    float speck = step (1.0 - 0.6 * P, hsp) * (1.0 - smoothstep (rr, rr + 0.06, length (dc))) * (1.0 - farK);
    covP = clamp (covP + speck, 0.0, 1.0);
    poppy = mix (vec3 (1.0, 0.42, 0.04), vec3 (1.0, 0.60, 0.10), fract (hsp * 7.1));
  }
  n = normalize (n);
  alb = mix (alb, poppy, covP);
  alb = mix (alb, vec3 (0.30, 0.28, 0.80), covL);
  transl = mix (transl, 1.35, max (covP, covL * 0.8));
  // The road: worn asphalt, a faded centre line, tar snakes, gravel shoulders.
  float roadK = 0.0, shoulder = 0.0;
  if (ad < 5.2) {
    roadK = 1.0 - smoothstep (3.05, 3.25, ad);
    shoulder = (1.0 - smoothstep (4.35, 4.85, ad)) * (1.0 - roadK);
    float wear = smoothstep (0.35, 0.0, abs (abs (ad - 1.5) - 0.75)) * 0.07;      // the wheels' polished tracks
    vec3 asph = vec3 (0.33, 0.31, 0.31) * (0.86 + 0.28 * (d1.a - 0.5) + 0.25 * (d2.a - 0.5)) + wear;
    float snake = (1.0 - smoothstep (0.010, 0.026, abs (d1.b - 0.55))) * step (0.55, d1.a);  // crack sealing, black
    asph = mix (asph, vec3 (0.10, 0.10, 0.11), snake * 0.75);
    vec2 lq = vPos.xz - uLoopC;
    float along = fract (atan (lq.y, lq.x) * (1.0 / 6.2831853) * uDashes);
    float dash = (1.0 - smoothstep (0.055, 0.085, ad)) * step (along, 0.28) * (0.45 + 0.55 * smoothstep (0.3, 0.7, d2.a));
    asph = mix (asph, vec3 (0.90, 0.76, 0.38), dash * 0.8);
    vec3 gravel = vec3 (0.86, 0.74, 0.56) * (0.80 + 0.40 * d2.a);
    alb = mix (alb, asph, roadK);
    alb = mix (alb, gravel, shoulder);
    // chalk drawings on the asphalt and the gravel shoulder, their tops toward the way, dusty: the grain of the road
    // shows through, the gravel's more
    float chalkOn = roadK * (0.75 + 0.25 * smoothstep (0.25, 0.75, d2.a)) * (1.0 - 4.0 * wear) + shoulder * (0.45 + 0.4 * d2.a);
    for (int i = 0; i < 4; ++i) {
      vec4 A = uChalkA[i], B = uChalkB[i];
      if (B.z < 0.5) continue;
      vec2 r = vPos.xz - A.xy;
      vec2 lq = vec2 (dot (r, vec2 (-A.w, A.z)), dot (r, A.zw)) / B.xy;     // across (right of the way), along
      if (abs (lq.x) < 1.0 && abs (lq.y) < 1.0) {
        vec4 C = uChalkC[i];
        vec4 ch = texture (uChalk, vec2 (mix (C.x, C.z, lq.x * 0.5 + 0.5), mix (C.y, C.w, 0.5 - lq.y * 0.5)));
        alb = mix (alb, ch.rgb * 0.95, ch.a * chalkOn);
      }
    }
    transl *= 1.0 - roadK - 0.3 * shoulder;
    n = normalize (mix (n, n0, (roadK + shoulder) * 0.9));
    covP *= 1.0 - roadK - shoulder;
  }
  // The peace sign, laid out in white flowers in the poppies on a far hillside (a rare find).
  if (uFind.y > 0.001) {
    vec3 q3 = vPos - uFindC;
    vec2 q = vec2 (dot (q3, uFindU), dot (q3, uFindV) / uFind.z) / uFind.x;
    float r = length (q);
    if (r < 1.2 && abs (dot (q3, cross (uFindU, uFindV))) < 0.6 * uFind.x * uFind.z) {
      float w = 0.105, s = 0.70710678;
      float inner = 1.0 - smoothstep (0.80, 0.84, r);
      float ring = 1.0 - smoothstep (w * 0.6, w, abs (r - 0.87));
      float stem = (1.0 - smoothstep (w * 0.6, w, abs (q.x))) * inner;
      float legL = (1.0 - smoothstep (w * 0.6, w, s * abs (q.y - q.x))) * step (q.x + q.y, 0.0) * inner;
      float legR = (1.0 - smoothstep (w * 0.6, w, s * abs (q.x + q.y))) * step (q.y, q.x) * inner;
      float m = max (max (ring, stem), max (legL, legR)) * uFind.y;
      float sp = fract (sin (dot (floor (vPos.xz * 1.3), vec2 (12.9898, 78.233))) * 43758.5453);
      vec3 white = mix (vec3 (0.99, 0.96, 0.88), vec3 (0.80, 0.70, 1.0), step (0.86, sp));
      float round_ = (1.0 - smoothstep (1.02, 1.18, r)) * uFind.y;
      alb = mix (alb, vec3 (1.0, 0.46, 0.05), round_ * 0.9);
      alb = mix (alb, white * (0.92 + 0.16 * sp), m);
      transl = mix (transl, 1.0, max (round_, m));
      covP = max (covP, round_ * (1.0 - m));
    }
  }
  // The light: the hills' shadows (per vertex), the oaks' (the shadow map), the poles' and a find's (drawn here).
  float sv = vSunV;
  if (dist < 330.0) sv *= shadowAt (vPos.xz).x;
  vec2 D = -uSunXZ;
  if (ad < 26.0 && dist < 240.0) sv *= poleShadow (vPos.xz, D);
  for (int i = 0; i < 2; ++i) {
    vec4 F = i == 0 ? uFindSh : uFindSh1; vec2 W = i == 0 ? uFindShW.xy : uFindShW.zw;
    if (W.y > 0.0) {
      vec2 ab = F.zw - F.xy; float t = clamp (dot (vPos.xz - F.xy, ab) / dot (ab, ab), 0.0, 1.0);
      float dd = length (vPos.xz - F.xy - ab * t);
      sv *= 1.0 - (1.0 - smoothstep (W.x * 0.6, W.x * (1.2 + 0.8 * t), dd)) * W.y * (1.0 - 0.4 * t);
    }
  }
  vec3 col = lightGround (alb, n, V, sv, transl);
  if (roadK + shoulder > 0.0) {
    vec3 R = reflect (V, n);
    float fres = pow (1.0 - max (-dot (V, n), 0.0), 4.0);
    col += uSunCol * pow (max (dot (R, uSun), 0.0), 16.0) * (0.25 + 0.75 * fres) * 0.6 * (roadK + 0.5 * shoulder) * sv;
    col += vHaze.rgb * fres * 0.25 * roadK;
  }
  // An attack: the near poppies flare as if the sun came through them all at once.
  float hk = hitK (dist) - 1.0;
  if (hk > 0.0) col += poppy * covP * hk * 0.45 * exp (-dist / 35.0) * (0.4 + 0.6 * sv);
  if (uBeamPool.w > 0.001) col += alb * vec3 (0.45, 0.95, 0.65) * 0.9 * beamPool (vPos.xz);   // under a saucer's beam
  col = mix (col, vHaze.rgb, vHaze.a);
  if (uDebug > 0.5) col = uDebug < 1.5 ? vec3 (vSunV) : uDebug < 2.5 ? vec3 (sv) : n * 0.5 + 0.5;
  fragColor = vec4 (col, 0.0);
}
