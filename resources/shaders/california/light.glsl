
uniform vec3 uSun;                  // toward the sun (unit), 8 degrees up
uniform vec2 uSunXZ;                // its azimuth on the ground (unit)
uniform float uSunTan;              // the tangent of its elevation
uniform vec3 uSunCol;               // its light on what faces it
uniform vec3 uZenith, uMidSky, uHorSun, uHorAway, uGlowCol;
uniform vec3 uAmbUp, uAmbDown;
uniform float uHazeK;               // 1 / the haze's reach
uniform sampler2D uShadow;          // R: the oaks' and the signs' shadows (the CPU's, round the van)
uniform vec2 uShadowOff;            // fract (uCamCell / 384)
uniform vec3 uShadowWin;            // the window's centre (rel xz) and its half size, metres
uniform vec2 uWind;                 // the breeze, m/s
// The answer to the playing: the last attack's age (s) and strength. A flash (0.25 s) and a glow (1.5 s), running out
// from the van at 200 m/s (the near field all at once, on the beat; the far hills a beat later). Long after an attack
// it is exactly 1: a silent frame is the frame the world always was.
uniform vec2 uHitT;
float hitK (float dist) {
  float t = uHitT.x - dist / 200.0;
  if (uHitT.y <= 0.0 || t < 0.0 || t > 9.0) return 1.0;
  return 1.0 + uHitT.y * (2.2 * exp (-t / 0.25) + 0.9 * exp (-t / 1.5));
}
float hitGlow () { return uHitT.y * (0.6 * exp (-uHitT.x / 0.25) + 0.4 * exp (-uHitT.x / 1.5)); }
// The sky: pale blue-lilac high, lilac-rose in the middle, apricot and gold at the horizon under the sun, a dusty
// rose away from it; the haze's glow round the low sun.
vec3 skyColour (vec3 V) {
  float e = max (V.y, 0.0);
  float mu = max (dot (V, uSun), 0.0);
  float hl = length (V.xz);
  vec2 hz = hl > 1.0e-4 ? V.xz / hl : vec2 (0.0, -1.0);
  float az = 0.5 + 0.5 * dot (hz, uSunXZ);
  vec3 hor = mix (uHorAway, uHorSun, az * az);
  vec3 c = mix (hor, uMidSky, smoothstep (0.02, 0.30, e));
  c = mix (c, uZenith, smoothstep (0.22, 0.85, e));
  c += uGlowCol * (0.50 * pow (mu, 28.0) + 0.30 * pow (mu, 6.0) + 0.10 * mu * mu) * (1.0 - 0.6 * smoothstep (0.0, 0.5, e));
  return c;
}
vec3 hazeColour (vec3 V) {
  vec3 h = skyColour (normalize (vec3 (V.x, 0.015, V.z)));
  return mix (h, vec3 (dot (h, vec3 (0.33))), 0.12);
}
float hazeAmount (float dist) { return 1.0 - exp (-dist * uHazeK); }
vec3 applyHaze (vec3 col, float dist, vec3 V) { return mix (col, hazeColour (V), hazeAmount (dist) * 0.94); }
// The far ridges' colour, layer k (0 the nearest): lit rose away from the sun, violet-blue toward it (backlit), in
// the haze, more of it the farther. The ground fades into layer 0 at its far edge.
vec3 ridgeFrom (vec3 V, vec3 haze, float k) {
  float hl = max (length (V.xz), 1.0e-4);
  float az = 0.5 + 0.5 * dot (V.xz / hl, uSunXZ);
  vec3 face = mix (vec3 (0.66, 0.42, 0.44), vec3 (0.34, 0.28, 0.50), smoothstep (0.35, 0.95, az));
  return mix (face, haze, 0.30 + 0.20 * k);
}
vec3 ridgeColour (vec3 V, float k) { return ridgeFrom (V, hazeColour (V), k); }
// The oaks' shadows at pr (rel xz): x, lit past them (1 lit).
vec2 shadowAt (vec2 pr) {
  float s = texture (uShadow, uShadowOff + pr / 384.0).r;
  vec2 q = abs (pr - uShadowWin.xy);
  float inside = 1.0 - smoothstep (uShadowWin.z - 16.0, uShadowWin.z - 4.0, max (q.x, q.y));
  return vec2 (1.0 - s * inside, 1.0);
}
// The light on a surface: the low sun wrapped round (a painter's soft terminator), the sky from above, the warm
// ground from below; and what shines through (grass, petals) when we look toward the sun.
vec3 lightGround (vec3 alb, vec3 n, vec3 V, float sunVis, float transl) {
  float ndl = dot (n, uSun);
  float wrap = max ((ndl + 0.35) / 1.35, 0.0);
  vec3 amb = mix (uAmbDown, uAmbUp, 0.5 + 0.5 * n.y);
  float back = pow (max (dot (V, uSun), 0.0), 5.0);
  return alb * (uSunCol * wrap * sunVis + amb) + alb * uSunCol * back * transl * sunVis;
}
