uniform sampler2D uNoise;
uniform vec2 uRes, uTan; uniform float uAspect, uTime;
uniform vec4 uWipe;      // start of the current / last wipe, start of the one before, when deposits began, stroke fraction 0..1 (< -9 = parked)
uniform vec4 uStroke;    // the current / last wipe's stroke out and its pause at the far end, the one before's (back as long as out)
uniform vec4 uRotor;     // glint phase (rad), disc tilt (rad), the shadow band's place on the glass (m), its strength
out vec4 o;
#include "titan/hash.glsl"
#include <glass>
bool onPane(int i, vec2 X){ vec4 m = WM[i]; return X.x > m.x + m.y * X.y && X.x < m.z + m.w * X.y; }
float invSmooth(float y){ return 0.5 - sin(asin(clamp(1.0 - 2.0 * y, -1.0, 1.0)) / 3.0); }   // inverse of smoothstep
vec4 h4(ivec2 c, uint s){ uint h = ih(uint(c.x) * 0x9E3779B1u ^ ih(uint(c.y) * 0x85EBCA77u ^ s)); return vec4(uvec4(h & 255u, (h >> 8) & 255u, (h >> 16) & 255u, h >> 24)) / 255.0; }   // four random bytes
float segDist(vec2 p, vec2 a, vec2 b){ vec2 pa = p - a, ba = b - a; float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0); return length(pa - ba * h); }
// where this pixel's ray meets the glass: (s across, t up the slope), metres. Plane through (0, YB, -ZB), normal (0, SB, CB).
vec2 glassST(vec2 ndc){
  vec3 d = vec3(ndc.x * uTan.x, (ndc.y - SHR) * uTan.y, -1.0);
  vec3 P = d * ((YB * SB - ZB * CB) / (d.y * SB - CB));
  return vec2(P.x, (P.y - YB) * CB - (P.z + ZB) * SB);
}
// the main rotor above the canopy, seen from below through the glass's top: a translucent disc (blades are a blur),
// a ring where the tips run, one soft glint travelling slowly round the rim. Fixed to the cab, never strobing.
vec4 rotorLayer(vec2 ndc){
  vec3 d = vec3(ndc.x * uTan.x, (ndc.y - SHR) * uTan.y, -1.0);
  float ct = cos(uRotor.y), st = sin(uRotor.y);
  vec3 N = vec3(0.0, ct, -st);                                 // tilted forward: the front of the disc is lower
  float den = dot(d, N);
  if (den <= 1e-4) return vec4(0.0);
  vec3 q = d * (dot(HUB, N) / den) - HUB;
  float rho = length(q);
  if (rho > ROT_R + 0.4) return vec4(0.0);
  float phi = atan(q.x, dot(q, vec3(0.0, -st, -ct)));
  float disc = 1.0 - smoothstep(ROT_R - 0.4, ROT_R + 0.05, rho);
  float tips = exp(-sq((rho - (ROT_R - 0.3)) / 0.25));
  float a = (0.09 + 0.07 * tips) * disc;
  float dphi = mod(phi - uRotor.x + 1.5707963, 3.14159265) - 1.5707963;          // two glints, opposite each other
  float glint = exp(-sq(dphi / 0.38)) * smoothstep(ROT_R - 2.4, ROT_R - 0.3, rho) * disc;
  return vec4(vec3(0.02, 0.018, 0.016) * a + vec3(0.30, 0.24, 0.16) * (glint * 1.15), a);    // premultiplied; the glint adds
}
// one blade's arc (wiper i, on its own pane only): is g in it, and when did that blade last cross it (out with an
// easing, a pause, back)?
float arcTime(vec2 g, int i, bool pane, out bool inA, out float gen, out float rho, out float phi){
  vec4 a = WA[i];
  vec2 rp = g - WP[i]; rho = length(rp); phi = atan(rp.y, rp.x); gen = 0.0;
  float x = (phi - a.x) / (a.y - a.x);
  inA = pane && rho > a.z && rho < a.w && x > 0.0 && x < 1.0;
  if (!inA) return -1e9;
  x = invSmooth(x);
  float tOut = uWipe.x + x * uStroke.x, tBack = uWipe.x + uStroke.x + uStroke.y + (1.0 - x) * uStroke.x;
  if (uTime >= tBack) { gen = uWipe.x; return tBack; }
  if (uTime >= tOut) { gen = uWipe.x; return tOut; }
  gen = uWipe.y; return uWipe.y + uStroke.z + uStroke.w + (1.0 - x) * uStroke.z;
}
// the line of pushed muck at an arc's limit: its outer and inner radius and the far end of the stroke
float smearBand(int i, float rho, float phi, float aa){
  vec4 a = WA[i];
  float x = (phi - a.x) / (a.y - a.x);
  float dOut = rho - a.w, dIn = a.z - rho, dEnd = (x - 1.0) * abs(a.y - a.x) * rho;
  bool inAng = x > 0.0 && x < 1.0;
  float band = 0.0;
  if (inAng && dOut > 0.0) band = 1.0 - smoothstep(0.0, 0.007 + aa, dOut);
  if (inAng && dIn > 0.0) band = max(band, 1.0 - smoothstep(0.0, 0.005 + aa, dIn));
  if (rho > a.z && rho < a.w && dEnd > 0.0) band = max(band, 1.0 - smoothstep(0.0, 0.009 + aa, dEnd));
  return band;
}
// wiper i lying on the glass at stroke fraction f, on its pane only: the arm rides on the blade's inner half; a
// rubber blade with its leading edge lit
vec4 wiperOver(vec4 acc, vec2 g, int i, float f, float pane, float aa){
  vec4 a = WA[i]; vec2 piv = WP[i]; float ang = mix(a.x, a.y, f);
  vec2 dir = vec2(cos(ang), sin(ang)), nrm = vec2(-dir.y, dir.x) * sign(a.y - a.x);
  float da = segDist(g, piv, piv + dir * (0.5 * (a.z + a.w)) + nrm * (0.007 * WW));
  float arm = (1.0 - smoothstep(0.008 * WW - aa, 0.008 * WW + aa, da)) * pane;
  float db = segDist(g, piv + dir * (a.z - 0.01), piv + dir * (a.w + 0.01));
  float blade = (1.0 - smoothstep(0.010 * WW - aa, 0.010 * WW + aa, db)) * pane;
  vec3 bc = vec3(0.012, 0.011, 0.012) + vec3(0.07, 0.05, 0.035) * smoothstep(0.002 * WW, 0.008 * WW, dot(g - piv, nrm));
  acc = vec4(vec3(0.035, 0.03, 0.03) * arm, arm) + acc * (1.0 - arm);
  return vec4(bc * blade, blade) + acc * (1.0 - blade);
}
void main(){
  vec2 uv = gl_FragCoord.xy / uRes;
  vec2 g = glassST(uv * 2.0 - 1.0);
  float aa = 0.75 * length(fwidth(g));                       // one pixel, measured on the glass
  vec4 acc = vec4(0.0);                                       // premultiplied layers, blended over the scene
  vec2 p = vec2(uv.x * uAspect, uv.y);                       // window heights (for the edge weighting)
  vec2 X = vec2(uv.x, 1.0 - uv.y) * CAB_PX;                 // the cabin's pixels, y down (for the panes)
  float tw = uWipe.z, gen = 0.0, band = 0.0, bph = 0.0, brh = 0.0, panes[NW];
  bool inAny = false;
  for (int i = 0; i < NW; i++) {
    bool pane = onPane(i, X), inA; float gi, rh, ph;
    panes[i] = pane ? 1.0 : 0.0;
    float ai = arcTime(g, i, pane, inA, gi, rh, ph);
    if (ai > tw) { tw = ai; gen = gi; }
    inAny = inAny || inA;
    float b = pane ? smearBand(i, rh, ph, aa) : 0.0;
    if (b > band) { band = b; bph = ph; brh = rh; }
  }
  float age = max(uTime - tw, 0.0);
  // more settles toward the edges and corners, in drifts
  float ex = 1.0 - smoothstep(0.0, 0.6, min(p.x, uAspect - p.x)), ey = 1.0 - smoothstep(0.0, 0.32, min(p.y, 1.0 - p.y));
  float w = 0.22 + 0.78 * clamp(max(ex, ey) + 0.7 * ex * ey, 0.0, 1.0);
  w *= 0.35 + 1.3 * textureLod(uNoise, g * 1.15 + vec2(0.13, 0.71), 0.0).g;
  float D = age * FLAKE_RATE * w; D = 1.3 * (1.0 - exp(-D / 1.3));
  float film = age * FILM_RATE * w; film = 0.5 * (1.0 - exp(-film / 0.5));
  if (!inAny && uWipe.x > -1e8) {
    if (band > 0.0) {
      float pushed = clamp((uWipe.x - max(uWipe.y + uStroke.z * 2.0 + uStroke.w, uWipe.z)) * FLAKE_RATE * w * 1.4, 0.0, 0.7);
      pushed *= smoothstep(uWipe.x, uWipe.x + uStroke.x, uTime);
      float streak = 0.55 + 0.45 * textureLod(uNoise, vec2(bph * 4.0, brh * 30.0), 0.0).a;
      film = max(film, band * pushed * streak);
    }
  }
  acc = rotorLayer(uv * 2.0 - 1.0);                                                // the rotor, beyond the glass
  if (uRotor.w > 0.001) {                                                          // its shadow sweeping over the glass
    float sb = exp(-sq((g.x - uRotor.z - 0.35 * g.y) / 0.16)) * uRotor.w;
    acc = vec4(0.0, 0.0, 0.0, sb) + acc * (1.0 - sb);
  }
  acc = vec4(vec3(0.075, 0.042, 0.018) * film, 0.3 * film) + acc * (1.0 - 0.3 * film);   // a warm brown veil on the glass
  // tholin flakes, laid out on the glass (so they shrink and flatten toward the top): one per hashed cell once the
  // deposit passes the cell's threshold; a fine dust between
  if (D > 0.015) {
    ivec2 gg = ivec2(int(gen * 7.0), int(gen * 3.0));
    vec2 cq = g / CELL; vec2 ci = floor(cq); vec2 cf = cq - ci; ivec2 cc = ivec2(ci) + gg;
    vec4 ha = h4(cc, 201u);
    float h = ha.x * 1.35 + 0.04;
    if (h < D) {
      vec4 hb = h4(cc, 202u);
      vec2 ctr = 0.3 + 0.4 * ha.yz;
      float rad = 0.09 + 0.42 * ha.w * ha.w;                                      // many small, a few big
      float an = 6.2831853 * hb.x, ca = cos(an), sa = sin(an); vec2 q = cf - ctr; q = vec2(ca * q.x - sa * q.y, sa * q.x + ca * q.y);
      q.y *= 1.0 + 0.9 * hb.y;                                                    // elongated
      float d0 = length(q) / rad;
      if (d0 < 1.7) {                                                             // only near the flake: the noise read
        vec2 nn = textureLod(uNoise, g * 88.0 + hb.zw * 7.0, 0.0).ag;
        float dd = d0 + 0.9 * (nn.x - 0.5) + 0.45 * (nn.y - 0.5);                // ragged, fluffy edges
        float a = (1.0 - smoothstep(0.6, 1.0, dd)) * smoothstep(h, h + 0.08, D) * (0.55 + 0.4 * fract(hb.x * 7.31));
        vec3 tint = mix(vec3(0.16, 0.09, 0.045), vec3(0.58, 0.34, 0.15), fract(hb.y * 5.17 + hb.z * 3.3));   // dark brown to rust and tan
        vec3 fc = tint + vec3(0.08, 0.045, 0.0) * smoothstep(0.3, 1.0, dd);
        float ka = a * 0.75;                                                      // a quarter of the light gets through
        acc = vec4(fc * a, ka) + acc * (1.0 - ka);
      }
    }
    vec2 fq = g / FINE; vec2 fi = floor(fq); vec4 hf = h4(ivec2(fi) + gg * 3, 211u);
    if (hf.x * 2.4 + 0.06 < D) {
      float dd = length(fq - fi - (0.3 + 0.4 * hf.yz)) / (0.12 + 0.16 * hf.w);
      float a = (1.0 - smoothstep(0.6, 1.0, dd)) * 0.55;
      acc = vec4(vec3(0.20, 0.12, 0.06) * a, a * 0.7) + acc * (1.0 - a * 0.7);
    }
  }
  if (uWipe.w > -9.0) for (int i = 0; i < NW; i++) acc = wiperOver(acc, g, i, uWipe.w, panes[i], aa);
  o = acc;
}
