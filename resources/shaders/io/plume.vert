// From the prototype's io-shaders.js (VS_PLUME), its body as it is.
#include "io/vtex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

in vec4 aE0;     // the vent (rel xyz), its kind
in vec4 aE1;     // its height (m), the dust's tint (0 pele .. 1 prometheus), a seed, its history row
in vec4 aE2;     // the lava's glow at its foot, the puff's age (s), its strength, -
uniform mat4 uViewProj; uniform vec3 uCamR, uCamU;
uniform sampler2D uHist;
uniform float uPxAng, uThinF;
out vec2 vUV; out vec4 vCol;
const float G = 1.796;
float histAt (float launch, float row) { int sl = int (floor (launch / 0.25)) & 255; return texelFetch (uHist, ivec2 (sl, int (row)), 0).r; }
vec3 lava (float t) { return mix (mix (vec3 (0.55, 0.06, 0.01), vec3 (1.0, 0.32, 0.04), smoothstep (0.0, 0.45, t)), vec3 (1.0, 0.82, 0.50), smoothstep (0.45, 1.0, t)); }
void main() {
  int pi = gl_VertexID / 6, ci = gl_VertexID - pi * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  vUV = corner;
  int kind = int (aE0.w + 0.5);
  float H = aE1.x, tint = aE1.y, row = aE1.w;
  uint sd = uint (aE1.z * 997.0) * 4096u + uint (pi);
  float r1 = hs (sd, 1u), r2 = hs (sd, 2u), r3 = hs (sd, 3u), r4 = hs (sd, 4u), r5 = hs (sd, 5u);
  vec3 P = aE0.xyz, col = vec3 (0.0);
  float size = 0.0, alpha = 0.0, minPx = 1.3;
  vec3 emit = vec3 (0.0);
  bool on = (pi & 1) == 0 || uThinF < 0.5;
  vec3 V = normalize (P - uCamRel);
  float fwd = max (dot (V, uSun), 0.0);
  vec3 sunS = uSunCol * uSunK * (0.50 + 0.50 * fwd + 2.5 * pow (fwd, 8.0)), jupS = uJupShine * 0.8;
  vec3 sunlit = sunS + jupS;
  if (kind == 0) {
    // the veil: 300 big soft puffs (a third of them in the column, the rest out to the canopy's rim); the grains: clumps
    // of frost and dust, the plume's texture and its sparkle in the Sun; the vent's own glow
    bool veil = false, core = pi >= 160;
    if (pi >= 176) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec4 (0.0); return; }
    // the launch: up to 45 degrees off the vertical (the far ones make the ring), every particle at one speed
    float col0 = veil ? step (r1, 0.33) : step (r1, 0.22);
    float th = col0 > 0.5 ? 0.16 * r4 : 0.785 * sqrt (fract (r1 * 7.31));
    float phi = r3 * TAU;
    float v0 = sqrt (2.0 * G * H);
    float vv = v0 * cos (th), vh = v0 * sin (th);
    float Tf = 2.0 * vv / G;
    float t = fract (uTime / Tf + r2) * Tf, u = t / Tf;
    float act = histAt (uTime - t, row);
    if (act < 0.01) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec4 (0.0); return; }
    float k = 0.50 + 0.50 * act;
    float wob = 1.0 + 0.10 * (r4 - 0.5);
    vec3 off = k * vec3 (vh * t * cos (phi) * wob, vv * t - 0.5 * G * t * t, vh * t * sin (phi) * wob);
    if (core) off = vec3 ((r4 - 0.5) * 6.0, H * 0.02 * r5, (r3 - 0.5) * 6.0);
    P += off;
    sunlit = sunS * ioTerm (P) + jupS;               // its crown keeps the Sun after sunset
    float hgt = off.y;
    vec3 dust = mix (vec3 (0.96, 0.88, 0.74), vec3 (0.88, 0.92, 1.0), tint);
    // its own glow: the gas excited blue (it shows in the dark), red heat low over the vent
    vec3 glow = vec3 (0.32, 0.48, 1.0) * (0.035 + 0.30 * uDark) * (1.0 - 0.6 * u) + vec3 (1.0, 0.35, 0.08) * aE2.x * exp (-hgt / 22.0) * 1.2;
    if (core) {
      size = 6.0 + 8.0 * r5;
      col = vec3 (1.0, 0.50, 0.18) * (0.3 + 1.2 * aE2.x) * act * (0.7 + 0.3 * sin (uTime * 3.0 + r1 * 20.0));
      emit = col; alpha = 0.0; minPx = 2.0;
    } else if (veil) {
      size = H * k * (0.045 + 0.12 * u) * (0.75 + 0.5 * r5);
      alpha = 0.11 * act * smoothstep (0.0, 0.05, u) * (1.0 - smoothstep (0.72, 1.0, u)) * (0.6 + 0.4 * r4) * (col0 > 0.5 ? 1.3 : 1.0);
      col = dust * sunlit + glow;
      emit = col * alpha;
      minPx = 3.0;
    } else {
      size = (0.6 + 1.6 * u) * k * (0.5 + r5);
      alpha = 0.22 * act * smoothstep (0.0, 0.03, u) * (1.0 - smoothstep (0.85, 1.0, u)) * (0.4 + 0.6 * r5);
      float glint = pow (max (sin (uTime * (1.0 + 2.0 * r4) + r5 * 50.0), 0.0), 16.0);
      col = dust * sunlit * (1.2 + 2.0 * glint * uSunK) + glow * 1.5;
      emit = col * alpha;
      minPx = 1.2;
    }
  } else if (kind == 1) {
    // a lava fountain: bombs at up to 20 degrees off the vertical, a range of speeds, cooling as they arc down slowly
    if (pi >= 180) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec4 (0.0); return; }
    float th = 0.35 * r1, phi = r3 * TAU;
    float v0 = sqrt (2.0 * G * H) * (0.45 + 0.55 * sqrt (r4));
    float vv = v0 * cos (th), vh = v0 * sin (th);
    float Tf = 2.0 * vv / G + 0.4;
    float t = fract (uTime / Tf + r2) * Tf, u = t / Tf;
    float act = histAt (uTime - t, row);
    if (act < 0.01) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec4 (0.0); return; }
    P += vec3 ((r5 - 0.5) * 3.0, 0.0, (r4 - 0.5) * 3.0) + vec3 (vh * t * cos (phi), max (vv * t - 0.5 * G * t * t, 0.0), vh * t * sin (phi));
    if (pi < 30 && pi >= 12) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec4 (0.0); return; }
    size = (pi < 12 ? 3.0 + 1.5 * r5 : 0.6 + 0.9 * r5);
    float heat = 1.0 - 0.75 * u;
    col = lava (heat) * (pi < 12 ? 0.8 : 2.6) * (0.6 + 0.4 * r4);
    alpha = act * (pi < 12 ? 0.0 : 0.15) * (1.0 - smoothstep (0.92, 1.0, u));
    emit = col * act * (1.0 - smoothstep (0.92, 1.0, u));
    minPx = 1.3;
  } else {
    // a puff: the vent answers an attack — a burst of gas and glowing bits, thrown up and out, falling back
    if (pi >= 260) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec4 (0.0); return; }
    float t = aE2.y - r1 * 0.15;
    if (t <= 0.0 || t > 7.0) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec4 (0.0); return; }
    float th = 0.9 * r2, phi = r3 * TAU, v0 = (14.0 + 26.0 * r4) * (0.6 + 0.4 * aE2.z);
    vec3 d = vec3 (sin (th) * cos (phi), cos (th), sin (th) * sin (phi));
    P += vec3 ((r5 - 0.5) * aE1.y, 0.0, (r1 - 0.5) * aE1.y) + d * v0 * t + vec3 (0.0, -0.5 * G * t * t, 0.0);
    P.y = max (P.y, aE0.y);
    sunlit = sunS * ioTerm (P) + jupS;
    float u = t / 7.0;
    bool hot = pi < 120;
    size = hot ? 0.5 + 0.4 * r5 : 3.0 + 14.0 * u;
    col = hot ? lava (1.0 - u * 1.2) * 2.5 * (1.0 - smoothstep (0.4, 0.9, u)) : (vec3 (0.85, 0.88, 0.95) * sunlit + vec3 (0.30, 0.45, 1.0) * 0.12) * (1.0 - u);
    alpha = hot ? 0.0 : 0.12 * (1.0 - u) * smoothstep (0.0, 0.05, u);
    alpha *= aE2.z;
    emit = (hot ? col : col * alpha) * aE2.z;
    minPx = hot ? 1.3 : 3.0;
  }
  if (size <= 0.0 || ! on) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec4 (0.0); return; }
  // never smaller than a pixel or so on the screen; what is smaller keeps its light (its coverage carried in its colour)
  float dist = length (P - uCamRel);
  float px = size / max (dist * uPxAng, 1e-4);
  float grow = max (px, minPx) / max (px, 1e-4);
  float cover = 1.0 / (grow * grow);
  size *= grow;
  vec3 wp = P + uCamR * ((corner.x - 0.5) * 2.0 * size) + uCamU * ((corner.y - 0.5) * 2.0 * size);
  vCol = vec4 (emit * cover, alpha * (kind == 0 ? 0.8 : 0.3) * cover);
  gl_Position = project (uViewProj, wp);
}
