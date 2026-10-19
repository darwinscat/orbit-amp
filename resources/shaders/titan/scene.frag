uniform vec2 uRes;
uniform vec3 uCamR, uCamU, uCamF, uHR, uHF;
uniform vec2 uTan; uniform float uShear, uCamY, uTime, uExposure;
uniform sampler2D uRipple, uNoise, uProfile;
uniform vec2 uRip0, uRip1, uRip2, uMotion, uFog, uCloud, uOil;
uniform vec4 uOilN;      // the oil film's 23-m detail and the TURBO streak's 19-m noise: their own wrapped offsets
uniform vec3 uSun, uSunR, uSunU;
uniform vec3 uHorizon, uZenith, uGlow, uMist, uLand, uBody;
uniform ivec2 uMagicBase; uniform vec2 uMagicFrac;
uniform vec3 uSat, uSatR, uSatU, uSatL; uniform float uSatVis;
uniform vec3 uVolcF, uVolcR; uniform float uVolcTop, uErupt;
uniform vec3 uStormF, uStormR; uniform float uFlash; uniform vec3 uFlashP;
uniform vec4 uField[2]; uniform vec2 uFieldSeed;
uniform float uWhite, uDown, uDash;
uniform vec2 uLamp;      // the searchlights, left and right, 0..1
uniform int uFlags;
out vec4 o;
#include "titan/hash.glsl"
#include <world>
#include "titan/util.glsl"
#include <scene>

// THE SEARCHLIGHTS, under the nose either side, aimed down at the sea ahead and a little out: where a lamp
// is (eye-relative), where its axis points, and how much of it lights a point in its cone, falling off
// with distance as light does.
const vec3 LAMP_C = vec3(1.0, 0.93, 0.80) * 1.2;
const float LAMP_SIDE = 1.6, LAMP_FWD = 1.2, LAMP_DROP = 1.4, AIM_SIDE = 5.5, AIM_FWD = 26.0, LAMP_COS_IN = 0.9986, LAMP_COS_OUT = 0.9925;
vec3 lampAt(int k){ float sd = k == 0 ? -1.0 : 1.0; return uHR * (sd * LAMP_SIDE) + uHF * LAMP_FWD + vec3(0.0, uCamY - LAMP_DROP, 0.0); }
vec3 lampAxis(int k){ float sd = k == 0 ? -1.0 : 1.0; vec3 aim = uHR * (sd * AIM_SIDE) + uHF * AIM_FWD; return normalize(vec3(aim.x, 0.0, aim.z) - lampAt(k)); }
float lampOn(vec3 q, int k, out vec3 l){
  vec3 d = q - lampAt(k); float r2 = max(dot(d, d), 4.0); l = d * inversesqrt(r2);
  return smoothstep(LAMP_COS_OUT, LAMP_COS_IN, dot(l, lampAxis(k))) * (AIM_FWD * AIM_FWD / r2);
}
#include <ripples>

vec3 skyBase(vec3 d){
  float el = max(d.y, 0.0);
  vec3 c = mix(uHorizon, uZenith, pow(smoothstep(0.0, 0.45, el), 0.5));
  float cs = dot(d, uSun);
  c += uGlow * (0.5 * exp((cs - 1.0) * 5.0) + 0.55 * exp((cs - 1.0) * 30.0));   // aureole ~25 deg, ~10 deg
  return c;
}
// the sun's core as an anisotropic gaussian (elevation / azimuth widths squared, rad^2), energy kept
float sunCore(vec3 d, float wv2, float wh2){
  vec3 dd = d - uSun; float a = dot(dd, uSunU), b = dot(dd, uSunR);
  return exp(-0.5 * (a * a / wv2 + b * b / wh2)) * CORE_W2 / sqrt(wv2 * wh2);
}
// ---- Saturn, ghostly behind the haze: far half of the rings, the banded disc, the near half
vec3 saturnOver(vec3 col, vec3 d, float pxA){
  float fz = dot(d, uSat);
  if (fz < 0.985 || uSatVis < 0.002) return col;
  vec2 q = vec2(dot(d, uSatR), dot(d, uSatU)) / (fz * SAT_RAD);      // planet radii
  vec2 r = rot(q, SAT_TC, SAT_TS);                                     // x along the ring line
  float aa = pxA / SAT_RAD + 0.07;                                    // the haze blurs its edge
  float T = uSatVis * 0.55;                                            // never fully clear
  // the haze thins around it while it shows
  col *= 1.0 - 0.10 * T * exp(-dot(q, q) / 9.0);
  float er = length(vec2(r.x, r.y / RING_OPEN));
  float ringA = smoothstep(1.22, 1.30, er) * (1.0 - smoothstep(2.18, 2.27, er));
  ringA *= 1.0 - 0.8 * smoothstep(1.90, 1.93, er) * (1.0 - smoothstep(1.99, 2.02, er));   // the Cassini division
  ringA *= 0.5 * clamp(RING_OPEN * er * 2.0 / max(aa, 1e-4), 0.25, 1.0);                // thin line: no shimmer
  vec3 ringC = vec3(0.70, 0.62, 0.48) * (0.85 + 0.15 * sin(er * 31.0));
  if (r.y > 0.0) col = mix(col, ringC, ringA * T);                     // far half, behind the disc
  vec2 pq = vec2(r.x, r.y / 0.9); float pr = length(pq);
  float disc = 1.0 - smoothstep(1.0 - aa, 1.0 + aa, pr);
  if (disc > 0.0) {
    float z = sqrt(max(1.0 - pr * pr, 0.0));
    float lat = pq.y;
    float band = 0.5 + 0.17 * sin(lat * 8.5 + 0.7) + 0.08 * sin(lat * 21.0 + 2.0);   // bands softened by the haze
    vec3 pc = mix(vec3(0.66, 0.56, 0.40), vec3(0.82, 0.74, 0.56), clamp(band, 0.0, 1.0));
    pc = mix(pc, vec3(0.55, 0.56, 0.62), smoothstep(0.75, 0.95, abs(lat)));   // greyer poles
    float lit = smoothstep(-0.15, 0.45, dot(vec3(pq, z), uSatL));
    vec3 planet = pc * (0.08 + 0.92 * lit) * (0.55 + 0.45 * z);
    planet *= 1.0 - 0.6 * exp(-sq((r.y + 0.16) / 0.05)) * lit;          // the rings' shadow band
    planet = mix(planet, col, 0.2);                                      // a little of the haze in front
    col = mix(col, planet, disc * T);
  }
  col += vec3(0.07, 0.056, 0.036) * (T * exp(-max(pr - 1.0, 0.0) * 5.0) * (1.0 - disc));   // a soft halo in the haze
  if (r.y <= 0.0) col = mix(col, ringC, ringA * T);                    // near half, in front
  return col;
}
// ---- the cryovolcano: a cone on the horizon and its vapour column (swells and spreads in an eruption)
vec3 volcanoOver(vec3 col, vec3 d, float pxT, vec3 hazeH){
  float fz = dot(d.xz, uVolcF.xz);
  if (fz < 0.93) return col;
  float X = dot(d, uVolcR) / fz, Y = d.y / fz;
  float top = uVolcTop;
  if (abs(X) > 0.42 || Y > top + 0.17 || Y < -0.004) return col;
  float hb = top - Y;
  if (hb > -0.003) {
    float rel = clamp(hb / top, 0.0, 1.0);
    float hw = 0.006 + hb / 0.58 * (1.0 + 1.1 * rel * rel);
    float a = (1.0 - smoothstep(hw - pxT, hw + pxT, abs(X))) * smoothstep(-pxT, pxT, hb);
    if (a > 0.0) {
      float streak = textureLod(uNoise, vec2(X / max(hw, 0.004) * 0.3 + 0.5, hb * 2.5 + 0.2), 0.0).b;
      vec3 cc = mix(uLand * 1.2, VAP_SH * 0.5, smoothstep(0.55, 0.8, streak) * (1.0 - 0.6 * rel));   // frost streaks
      cc *= 0.8 + 0.4 * smoothstep(-0.5, 1.0, X / max(hw, 0.004));                                  // lit from the right
      cc = mix(cc, hazeH, 1.0 - exp(-16000.0 / (LAND_HAZE_L * 1.6)));
      col = mix(col, cc, a);
    }
  }
  float yy = Y - top;
  if (yy > -0.006) {
    float e = uErupt;
    float Hc = mix(0.04, 0.092, e);
    float xc = 0.18 * yy + 1.4 * yy * yy;                                 // the wind bends it to the right
    float w = 0.005 + 0.004 * e + yy * (0.12 + 0.30 * e);
    float colm = exp(-sq((X - xc) / w)) * (1.0 - smoothstep(0.7 * Hc, 1.1 * Hc, yy));
    float capW = 0.02 + 0.2 * e;
    float cap = exp(-sq((yy - 0.92 * Hc) / (0.009 + 0.016 * e))) * exp(-sq((X - xc - 0.3 * capW) / capW)) * (0.2 + 0.8 * e);
    float dens = max(colm, cap) * smoothstep(-0.006, 0.002, yy);
    if (dens > 0.003) {
      vec4 n = textureLod(uNoise, vec2(X * 4.0 + 0.3, yy * 4.0 - uTime * (0.008 + 0.02 * e)), 1.5);
      vec4 n2 = textureLod(uNoise, vec2(X * 10.0 + 0.1, yy * 10.0 - uTime * (0.02 + 0.04 * e)), 2.5);
      dens *= 0.35 + 1.0 * n.a + 0.5 * (n2.a - 0.5);
      float a = clamp(dens * (0.55 + 0.4 * e), 0.0, 0.94);
      vec3 pc = mix(VAP_SH, VAP_LIT, clamp(0.25 + 0.9 * n2.a + 0.6 * (X - xc) / (w + capW * step(0.8 * Hc, yy)), 0.0, 1.0));
      pc *= 0.62;                                                        // lit only by the haze
      pc = mix(pc, hazeH, 0.3);
      col = mix(col, pc, a);
    }
  }
  return col;
}
float dome(float X, float c, float w, float h){ float q = (X - c) / w; return h * sqrt(max(1.0 - q * q, 0.0)); }
// ---- the storm far off: towers, an anvil, a rain curtain; lightning lights it and the haze around
vec3 stormOver(vec3 col, vec3 d, float pxT, vec3 hazeH){
  float fz = dot(d.xz, uStormF.xz);
  if (fz < 0.92) return col;
  float X = dot(d, uStormR) / fz, Y = d.y / fz;
  if (abs(X) > 0.42 || Y > 0.2 || Y < -0.004) return col;
  vec2 fd = vec2(X, Y) - uFlashP.xy;
  float fl = uFlash;
  if (abs(X) < 0.3 && Y < 0.13) {
    float top = max(max(dome(X, -0.075, 0.062, 0.060), dome(X, 0.02, 0.056, 0.090)),
                    max(dome(X, 0.115, 0.046, 0.050), dome(X, -0.17, 0.05, 0.034)));
    top = max(top, max(dome(X, 0.058, 0.032, 0.072), dome(X, -0.028, 0.03, 0.056)));
    vec4 n = textureLod(uNoise, vec2(X * 4.0 + 0.7, Y * 4.0 + 0.2), 1.0);
    vec4 n2 = textureLod(uNoise, vec2(X * 9.0 + 0.3, Y * 9.0 + 0.6), 2.0);
    top += 0.011 * (n.a - 0.5) + 0.008 * (n2.a - 0.5);                              // billowing tops
    float base = 0.022 + 0.008 * (n2.g - 0.5);
    float soft = 2.0 * pxT + 0.004;
    float body = smoothstep(top + soft, top - soft, Y) * smoothstep(base - 0.006, base + 0.006, Y);
    float ax = abs(X - 0.03);
    float anvTop = 0.104 + 0.002 * (n2.g - 0.5);                                   // flat on top, like a lid
    float anvBot = 0.084 + 0.16 * ax * ax + 0.003 * (n.a - 0.5);                   // its underside sags toward the tower
    float anv = smoothstep(anvBot - 0.003, anvBot + 0.003, Y) * (1.0 - smoothstep(anvTop - 0.002, anvTop + 0.002, Y)) * (1.0 - smoothstep(0.08, 0.13, ax));
    anv = max(anv, smoothstep(0.065, 0.085, Y) * (1.0 - smoothstep(0.03, 0.06, ax)) * (1.0 - smoothstep(anvTop - 0.002, anvTop + 0.002, Y)));   // the tower flares into it
    float cloud = clamp(max(body, 0.8 * anv), 0.0, 1.0);
    float shafts = 0.35 + 0.65 * textureLod(uNoise, vec2(X * 30.0, Y * 1.5 + 0.5), 0.5).g;
    float rain = (1.0 - smoothstep(base * 0.6, base, Y)) * (1.0 - smoothstep(0.05, 0.13, abs(X - 0.01))) * shafts;
    vec3 cc = mix(STORM_D, STORM_L, clamp((Y - base) / max(top - base, 0.01) * 0.9 + 0.7 * (n2.a - 0.5), 0.0, 1.0));
    cc += FLASH * fl * (3.2 * exp(-dot(fd, fd) / 0.0011) + 0.35);       // lit from inside
    cc = mix(cc, hazeH, 0.45);
    col = mix(col, cc * 0.8, rain * 0.5 * (1.0 - cloud));
    col = mix(col, cc, cloud * 0.9);
    if (uFlashP.z > 0.5 && fl > 0.25 && Y < uFlashP.y && Y > -0.002) {   // the bolt down to the sea: a jagged random walk
      float sY = Y / 0.0055; float i0 = floor(sY); float fY = sY - i0;
      uint seed = uint(uFlashP.x * 9973.0 + 5000.0);
      float j0 = h01(uint(i0 + 4.0) * 2u, seed) - 0.5, j1 = h01(uint(i0 + 5.0) * 2u, seed) - 0.5;
      float sY2 = Y / 0.0016; float k0 = floor(sY2);
      float jj = mix(h01(uint(k0 + 4.0) * 2u + 1u, seed) - 0.5, h01(uint(k0 + 5.0) * 2u + 1u, seed) - 0.5, sY2 - k0);
      float jag = mix(j0, j1, fY) * 0.009 + jj * 0.0025 + (Y - uFlashP.y) * 0.25 * (h01(5u, seed) - 0.5);
      col += FLASH * exp(-sq((X - uFlashP.x - jag) / (pxT * 0.8))) * fl * 3.0;
    }
  }
  col += FLASH * fl * (0.22 * exp(-dot(fd, fd) / 0.01) + 0.06 * exp(-dot(fd, fd) / 0.06));   // the haze lights up
  return col;
}
float dec16(vec2 t){ return (t.x * 255.0 + t.y) / 255.0; }
// the shore bands composited far to near over col; xs = screen column, e = elevation tangent, aa = edge width
vec3 landOver(vec3 col, float xs, float e, float aa, vec3 hazeH, inout float hitD){
  float u = (xs - PROF_X0) / (PROF_X1 - PROF_X0);
  if (u <= 0.0 || u >= 1.0) return col;
  float top = TAN_MIN + (TAN_MAX - TAN_MIN) * dec16(textureLod(uProfile, vec2(u, 5.5 / 6.0), 0.0).rg);
  if (e > top + aa) return col;
  for (int k = 4; k >= 0; k--) {
    vec4 t = textureLod(uProfile, vec2(u, (float(k) + 0.5) / 6.0), 0.0);
    float E = TAN_MIN + (TAN_MAX - TAN_MIN) * dec16(t.rg);
    float a = clamp((E - e) / aa + 0.5, 0.0, 1.0);
    if (a > 0.0) {
      float D = mix(BMIN[k], BMAX[k], t.b);
      float below = clamp((E - e) / max(E + 0.004, 0.004), 0.0, 1.0);    // 0 at the crest, 1 at the waterline
      vec3 lc = uLand * mix(1.1, 0.7, t.a) * mix(1.3, 0.85, sqrt(below));
      lc = mix(lc, hazeH, 1.0 - exp(-D / LAND_HAZE_L));
      col = mix(col, lc, a);
      hitD = mix(hitD, D, a);
    }
  }
  return col;
}
// everything above the sea in direction d (used for the sky and, mirrored, for the reflection)
vec3 skyScene(vec3 d, float pxT, float aaLand, vec3 hazeH, float corePlus, float gam, inout float hitD){
  vec3 c = skyBase(d) + uGlow * (CORE_AMP * sunCore(d, CORE_W2 + 2.0 * corePlus, CORE_W2 + 2.0 * corePlus * gam * gam));
  c = saturnOver(c, d, pxT + 1.4 * sqrt(corePlus));
  c = stormOver(c, d, pxT, hazeH);
  c = volcanoOver(c, d, pxT, hazeH);
  if ((uFlags & 1) != 0) {
    float e = d.y / length(d.xz);
    float xs = dot(d, uHR) / max(dot(d, uHF), 1e-3) / uTan.x;
    c = landOver(c, xs, e, aaLand, hazeH, hitD);
  }
  return c;
}
// one ripple layer: slope (world xz) and its sub-pixel variance, filtered by the pixel footprint and the motion
void ripLayer(vec2 off, float L, float c, float s, float w, vec2 p, vec2 px, vec2 py, inout vec2 sl, inout float var){
  vec2 uv = off + rot(p, c, s) / L;
  vec2 gx = rot(px, c, s) / L, gy = rot(py, c, s) / L, gm = rot(uMotion, c, s) / L;
  if (dot(gm, gm) > dot(gy, gy)) gy = gm;
  vec4 r = textureGrad(uRipple, uv, gx, gy);
  vec2 sv = (r.rg * 2.0 - 1.0) * w;
  sl += rotT(sv, c, s);
  var += max(r.b * 2.0 * w * w - dot(sv, sv), 0.0);
}
float mistPatch(vec2 xz, float lod){
  float n = textureLod(uNoise, uFog + xz / MIST_L, lod).g;
  return 0.03 + 1.2 * smoothstep(0.5, 0.78, n);
}
// optical depth of the mist layer between s0 and s1 along dir (closed form for an exponential height profile)
float mistSeg(vec3 dir, float s0, float s1, float pt){
  if (s1 <= s0) return 0.0;
  float y0 = dir.y * s0 + uCamY;
  float kk = dir.y * (s1 - s0) / MIST_HS;
  float g = abs(kk) < 1e-3 ? 1.0 - 0.5 * kk : (1.0 - exp(-kk)) / kk;
  return MIST_DENS * pt * exp(-max(y0, 0.0) / MIST_HS) * (s1 - s0) * g;
}
void main(){
  vec2 ndc = gl_FragCoord.xy / uRes * 2.0 - 1.0;
  vec3 dir = normalize(uCamF + (ndc.x * uTan.x) * uCamR + ((ndc.y - uShear) * uTan.y) * uCamU);
  vec3 ddx = dFdx(dir), ddy = dFdy(dir);
  float pxT = 2.0 * uTan.y / uRes.y;
  vec3 dH = normalize(vec3(dir.x, 0.0, dir.z));
  vec3 hazeH = skyBase(dH) + uGlow * (CORE_AMP * sunCore(dH, CORE_W2, CORE_W2));   // what distance fades to
  vec3 col; float tEnd = 6000.0;
  float foamM = 0.0;
  if (dir.y > -0.0012) {
    // ---- sky, Saturn, storm, volcano, the far shore
    float hd = tEnd;
    vec3 base = skyBase(dir);
    col = skyScene(dir, pxT, pxT, hazeH, 0.0, 0.0, hd);
    if ((uFlags & 16) != 0 && dir.y > 0.012) {   // faint high cloud streaks, stretched along x
      vec2 cuv = uCloud + (dir.xz / dir.y) * (CLOUD_H / CLOUD_L) * vec2(0.35, 1.0);
      float fp = pxT * CLOUD_H / (dir.y * dir.y) / CLOUD_L * 256.0;
      float n = textureLod(uNoise, cuv, clamp(log2(max(fp, 1.0)), 0.0, 7.0)).a;
      col += base * (smoothstep(0.02, 0.2, dir.y) * CLOUD_AMP * (n - 0.5) * 2.0);
    }
    tEnd = min(hd, tEnd);
  } else {
    // ---- the sea: an analytic plane, ripples from a slope map
    float t = uCamY / -dir.y;
    vec2 p = dir.xz * t;
    float ky = uCamY / (dir.y * dir.y);
    vec2 px = dir.xz * (ky * ddx.y) + ddx.xz * t, py = dir.xz * (ky * ddy.y) + ddy.xz * t;
    float fpm = max(length(px), length(py));                            // metres per pixel here
    float fpg = sqrt(length(px) * length(py));                          // the same, geometric mean (sharper at grazing)
    // oily slicks: calmer water with a thin-film sheen
    float slick = 0.0, film = 0.5, sheen = 0.0;
    if (t < 1500.0) {
      vec4 fo = textureLod(uNoise, uOil + p / OIL_L, log2(max(fpm / OIL_L * 256.0, 1.0)));
      slick = smoothstep(0.52, 0.7, fo.r) * (1.0 - smoothstep(500.0, 1500.0, t));
      if (t < 100.0) {
        float f2 = textureLod(uNoise, uOilN.xy + p / 23.0, log2(max(fpm / 23.0 * 256.0, 1.0)) + 1.5).g;
        film = fo.g * 1.6 + f2 * 1.2;
        sheen = slick * (1.0 - smoothstep(35.0, 100.0, t));
      }
    }
    // nitrogen "champagne": boiling froth with popping bubbles
    float bub = 0.0;
    if ((uFlags & 32) != 0) {
      for (int i = 0; i < 2; i++) {
        vec4 Fd = uField[i];
        if (Fd.w <= 0.001) continue;
        vec2 dq = p - Fd.xy;
        float dd = dot(dq, dq);
        if (dd > Fd.z * Fd.z * 1.7) continue;
        float sd = i == 0 ? uFieldSeed.x : uFieldSeed.y;
        float n = textureLod(uNoise, dq / 80.0 + vec2(sd, sd * 1.7), 0.0).g;
        float m = (1.0 - smoothstep(0.45, 1.0, sqrt(dd) / Fd.z + 0.6 * (n - 0.5))) * Fd.w;
        if (m <= 0.0) continue;
        float lodF = log2(max(sqrt(fpg * fpm) / 5.0 * 256.0, 1.0));
        float froth = 0.62 * textureLod(uNoise, dq / 5.0 + vec2(sd * 3.1, 0.0) + uTime * vec2(0.031, -0.023), lodF + 2.0).a
                    + 0.38 * textureLod(uNoise, dq / 1.9 + vec2(0.0, sd * 2.3) - uTime * vec2(0.02, 0.05), lodF + 3.2).g;
        float b = 0.0;
        if (fpm < 0.25) {                                               // resolved: bubbles swell and pop
          vec2 cq = dq / 0.75; vec2 ci = floor(cq); vec2 cf = cq - ci;
          ivec2 cc = ivec2(ci) + ivec2(i * 7919, 131);
          float h1 = hcell(cc, 91u), h2 = hcell(cc, 92u), h3 = hcell(cc, 93u);
          float ph = fract(uTime * (0.7 + 1.1 * h3) + h1);
          float br = 0.06 + 0.3 * ph;
          float dist = length(cf - vec2(0.2 + 0.6 * h1, 0.2 + 0.6 * h2));
          b = 0.6 * smoothstep(br + 0.14, br - 0.02, dist) * smoothstep(br - 0.26, br - 0.04, dist) * (1.0 - ph * ph) * (1.0 - smoothstep(0.08, 0.25, fpm));   // soft rims
        }
        float wide = 0.22 + 0.1 * clamp(lodF / 6.0, 0.0, 1.0);
        float f = m * (0.45 + 0.55 * smoothstep(0.5 - wide, 0.6 + wide, froth));
        foamM = max(foamM, f); bub = max(bub, b * m);
      }
    }
    // TURBO dash: the downwash tears the surface into a streak of foam ahead
    if (uDash > 0.01 && t < 70.0) {
      float fw = dot(p, uHF.xz), lat = dot(p, uHR.xz);
      vec4 nz = textureLod(uNoise, uOilN.zw + p / 19.0, log2(max(fpm / 19.0 * 256.0, 1.0)) + 1.0);
      float streak = (1.0 - smoothstep(0.6, 2.6, abs(lat) + 2.2 * (nz.g - 0.5))) * smoothstep(6.0, 12.0, fw) * (1.0 - smoothstep(20.0, 45.0, fw + 15.0 * (nz.a - 0.5)));
      foamM = max(foamM, uDash * streak * smoothstep(0.3, 0.75, nz.a + 0.25 * nz.g) * 0.7);
    }
    vec2 sl = vec2(0.0); float var = 0.0;
    ripLayer(uRip0, RIP_L0, RIP_C0, RIP_S0, RIP_W0 * (1.0 - 0.7 * slick), p, px, py, sl, var);
    ripLayer(uRip1, RIP_L1, RIP_C1, RIP_S1, RIP_W1 * (1.0 - 0.4 * slick), p, px, py, sl, var);
    ripLayer(uRip2, RIP_L2, RIP_C2, RIP_S2, RIP_W2, p, px, py, sl, var);
    // the rotor's downwash: rings of ripples running out from under the craft, strongest in a hover
    if (uDown > 0.01 && t < 90.0) {
      float rr = length(p); vec2 rd = p / max(rr, 0.01);
      float env = uDown * exp(-rr / 28.0) * (1.0 - smoothstep(0.12, 0.4, fpm));
      sl += rd * env * (0.075 * sin(rr * 3.9 - uTime * 5.5) + 0.04 * sin(rr * 1.7 - uTime * 3.1 + 1.3));
      var += env * 0.002;
    }
    // a "magic island": a rare, transient rougher, brighter patch on the surface
    float magic = 0.0;
    if ((uFlags & 8) != 0 && t < 5000.0) {
      vec2 f = uMagicFrac + p / MAGIC_CELL; vec2 fc = floor(f); vec2 lp = f - fc;
      ivec2 c = uMagicBase + ivec2(fc);
      if (hcell(c, 71u) < MAGIC_P) {
        vec2 cen = 0.3 + 0.4 * vec2(hcell(c, 72u), hcell(c, 73u));
        float an = 6.2831853 * hcell(c, 74u);
        vec2 q = rot(lp - cen, cos(an), sin(an)) * MAGIC_CELL;
        float rad = 70.0 + 110.0 * hcell(c, 75u);
        float nn = textureLod(uNoise, vec2(c) * 0.37 + q / 260.0, 0.0).a;
        float r = length(q / vec2(rad, rad * 0.42)) + 0.45 * (nn - 0.5);
        float life = smoothstep(0.35, 0.85, sin(6.2831853 * (uTime / MAGIC_T + hcell(c, 76u))));
        magic = (1.0 - smoothstep(0.55, 1.0, r)) * life;
      }
    }
    var += magic * 0.03 + foamM * 0.06;
    vec3 n = normalize(vec3(-sl.x, 1.0, -sl.y));
    vec3 R = reflect(dir, n); R.y = max(abs(R.y), 0.0012);
    float gam = -dir.y;
    float hd = 0.0;
    vec3 refl = skyScene(R, pxT, pxT + 1.5 * sqrt(var), hazeH, var, gam, hd);
    float cosi = clamp(dot(-dir, n), 0.0, 1.0);
    // thin-film sheen: an oily rainbow over the orange reflection, strongest in the slicks
    vec3 irid = 0.5 + 0.5 * cos(6.2831853 * (film * 0.75 + uTime * 0.01 + vec3(0.0, 0.33, 0.67)));   // broad swirls, not flecks
    refl *= mix(vec3(1.0), 0.4 + 1.2 * irid, 0.45 * sheen);
    float Fr = 0.018 + 0.982 * pow(1.0 - cosi, 5.0);                   // liquid methane, n ~ 1.28
    col = uBody * (1.0 - Fr) + refl * (Fr * 0.62);                      // a dark liquid, a soft mirror
    col += magic * 0.07 * hazeH;
    // the froth sits on top: lit by the haze, violet in its hollows, bubble rims catch the light
    vec3 foamC = mix(FOAM_SH * 0.62, FOAM * 0.68, smoothstep(0.2, 0.9, foamM));
    col = mix(col, foamC, clamp(foamM * 1.1, 0.0, 0.92));
    col += FOAM * (bub * 0.6);
    // the searchlights' pools on the liquid: a little light in the body, the ripples glittering in it
    for (int k = 0; k < 2; k++) {
      float on = k == 0 ? uLamp.x : uLamp.y;
      if (on <= 0.001) continue;
      vec3 l; float lit = lampOn(vec3(p.x, 0.0, p.y), k, l);
      if (lit <= 0.0) continue;
      float spec = pow(max(dot(n, normalize(-l - dir)), 0.0), 220.0);
      col += LAMP_C * (on * lit) * (0.07 * (1.0 - Fr) + 2.6 * spec + 0.3 * foamM);
    }
    col = mix(col, hazeH, 1.0 - exp(-t / HAZE_L));
    tEnd = t;
  }
  // ---- low violet mist banks over the sea (and fizz vapour over a field)
  float pt0 = 0.0;
  vec3 mistC = mix(uMist, hazeH, 0.3);
  if ((uFlags & 2) != 0 && dir.y < 0.2) {
    float s0e = min(tEnd, 150.0), s1e = min(tEnd, 900.0), s2e = min(tEnd, 2500.0);
    pt0 = mistPatch(dir.xz * (0.4 * s0e), 0.0) * (1.0 - 0.7 * uDown);   // the downwash blows the near mist thin
    float pt1 = mistPatch(dir.xz * (150.0 + 0.4 * max(s1e - 150.0, 0.0)), 0.5);
    float pt2 = mistPatch(dir.xz * (900.0 + 0.4 * max(s2e - 900.0, 0.0)), 1.5);
    float tau = mistSeg(dir, 0.0, s0e, pt0) + mistSeg(dir, 150.0, s1e, pt1) + mistSeg(dir, 900.0, s2e, pt2);
    col = mix(mistC, col, exp(-tau));
    col = mix(col, mistC * 1.15, 0.22 * foamM);
  }
  // ---- the searchlights' beams in the haze: what each cone lights along the ray, out to the sea or 90 m
  if (uLamp.x + uLamp.y > 0.001) {
    float far = min(tEnd, 90.0), beam = 0.0;
    for (int k = 0; k < 2; k++) {
      float on = k == 0 ? uLamp.x : uLamp.y;
      if (on <= 0.001) continue;
      for (int i = 0; i < 10; i++) {
        float tq = far * (float(i) + 0.5) / 10.0;
        vec3 l; beam += on * lampOn(vec3(0.0, uCamY, 0.0) + dir * tq, k, l) * far / 10.0;
      }
    }
    col += LAMP_C * mix(hazeH, vec3(1.0), 0.5) * (beam * 0.0005);
  }
  // ---- spray on the glass while ploughing through froth
  if (uWhite > 0.003) {
    vec2 sp = gl_FragCoord.xy / uRes;
    float n = textureLod(uNoise, vec2(sp.x * 6.0, sp.y * 1.2 + uTime * 0.06), 0.0).a;
    float n2 = textureLod(uNoise, vec2(sp.x * 21.0 + 0.3, sp.y * 4.0 - uTime * 0.13), 0.0).g;
    float zone = smoothstep(0.5, 0.0, sp.y) * (0.6 + 0.4 * smoothstep(0.45, 0.0, min(sp.x, 1.0 - sp.x)));
    float blot = smoothstep(0.45, 0.7, n * 0.6 + n2 * 0.6);
    float w = clamp(uWhite * zone * (0.15 + 0.85 * blot), 0.0, 0.6);
    col = mix(col, mix(FOAM_SH, FOAM, 0.6) * 0.7, w);
  }
  vec3 cc = toDisplay(col * uExposure);
  cc += (h01(uint(gl_FragCoord.x) + uint(gl_FragCoord.y) * 8191u, 3u) - 0.5) / 255.0;   // no banding in the dark
  o = vec4(cc, 1.0);
}
