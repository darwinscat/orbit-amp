// From the prototype's io-shaders.js (LIGHT), its body as it is.

uniform vec3 uSun; uniform vec3 uSunCol; uniform float uSunK;   // the Sun's way, its colour, how much of it shows
uniform vec3 uJup; uniform vec3 uJupShine;                      // Jupiter's way; its light on Io (its phase in it)
uniform float uDark;                                            // the darkness: 1 at night or in Jupiter's shadow
uniform vec4 uTerm;                                             // the key's height (rad), its way (xz), its disc's half width
uniform vec3 uAirglow;                                          // Io's own faint glow in the dark (the ground's share)
uniform vec4 uPL[4]; uniform vec4 uPLC[4]; uniform int uPLN;    // the lava's lights: position (rel), reach; colour, strength; how many
uniform vec4 uLamps;                                            // the rover's lamps: its place (rel xz), its way (xz)
uniform vec4 uSL[6]; uniform int uSLN; uniform vec3 uSLC;      // the hardware's work lights in the dark: place (rel), reach; colour x strength
uniform vec4 uSLBox;                                            // the box (rel xz) all their pools lie in: outside it nothing is looked at
uniform vec2 uHitT;                                             // the last attack's age (s) and strength
uniform highp sampler2DShadow uSh0, uSh1;                       // the near cascade (every frame) and the far (when the Sun moves)
uniform mat4 uShM0, uShM1;                                      // rel -> their [0,1] cubes
uniform vec4 uShK;                                              // the normal offsets (m) and depth biases of the two
uniform float uShOn;
float hitGlow () { if (uHitT.y <= 0.0 || uHitT.x > 9.0) return 0.0; return uHitT.y * (1.0 * exp (-uHitT.x / 0.25) + 0.5 * exp (-uHitT.x / 1.5)); }
// IO'S OWN SHADOW (its terminator, the sunset per point): the key seen from p (rel). The ground falls away as d^2/2R, so a
// point toward the key sees it higher by its distance over R and a point high over the datum (TERM_Y0) sees past the
// horizon by sqrt (2h/R): at sunset the plains lose the light first, the mesas, the far massifs and the plumes' crowns
// last (a 470 m crown keeps it to ~1.3 degrees under the horizon). The near terrain's own shadows are the cascades'.
const float TERM_Y0 = -10.0;
float ioTerm (vec3 p) {
  float e = uTerm.x + dot (p.xz - uCamRel.xz, uTerm.yz) / IO_R + sqrt (2.0 * max (p.y - TERM_Y0, 0.0) / IO_R);
  return smoothstep (-uTerm.w, uTerm.w, e);
}
// the Sun's light at p: 1 lit, 0 in a shadow (the near cascade where it reaches, fading into the far one at its edge;
// Io's own shadow over all)
float sunShadow (vec3 p, vec3 n) {
  if (uShOn < 0.5) return ioTerm (p);
  vec3 q0 = (uShM0 * vec4 (p + n * uShK.x, 1.0)).xyz;
  vec2 e0 = abs (q0.xy - 0.5);
  float m0 = max (e0.x, e0.y);
  float s0 = 1.0, s1 = 1.0;
  if (m0 < 0.5 && q0.z < 1.0) s0 = texture (uSh0, vec3 (q0.xy, q0.z - uShK.z));
  float w0 = 1.0 - smoothstep (0.40, 0.48, m0);
  if (w0 < 1.0) {
    vec3 q1 = (uShM1 * vec4 (p + n * uShK.y, 1.0)).xyz;
    vec2 e1 = abs (q1.xy - 0.5);
    if (max (e1.x, e1.y) < 0.5 && q1.z < 1.0) s1 = texture (uSh1, vec3 (q1.xy, q1.z - uShK.w));
  }
  return mix (s1, s0, w0) * ioTerm (p);
}
// the lava's light on a surface (a lake lights its walls and the plain round it; a fountain the road)
vec3 lavaLight (vec3 p, vec3 n) {
  vec3 c = vec3 (0.0);
  for (int i = 0; i < 4; ++i) {
    if (i >= uPLN) break;
    vec3 L = uPL[i].xyz - p; float d2 = dot (L, L);
    float ndl = max (dot (n, L * inversesqrt (d2 + 1e-3)), 0.0);
    c += uPLC[i].rgb * uPLC[i].w * (0.15 + 0.85 * ndl) / (1.0 + d2 / (uPL[i].w * uPL[i].w));
  }
  return c;
}
// the rover's lamps: a soft cone along the way, out to some 60 m (they show only in the dark)
float lampsAt (vec3 p) {
  vec2 q = p.xz - uLamps.xy; float d = length (q);
  float along = dot (q, uLamps.zw);
  float cone = smoothstep (0.55, 0.9, along / max (d, 1e-3));
  return cone * smoothstep (2.0, 8.0, along) / (1.0 + sq (d / 24.0)) * step (p.y, uCamRel.y + 6.0);
}
// the hardware's work lights in the dark (20.11, the owner: "in the dark light the stations, the vehicles and the
// finds"): a warm lamp over each of the nearest in view, a little toward us, lighting it and a pool of ground round it
vec3 siteLight (vec3 p, vec3 n) {
  vec3 c = vec3 (0.0);
  if (uSLN == 0 || p.x < uSLBox.x || p.z < uSLBox.y || p.x > uSLBox.z || p.z > uSLBox.w) return c;
  for (int i = 0; i < 6; ++i) {
    vec3 L = uSL[i].xyz - p; float d2 = dot (L, L), R2 = uSL[i].w * uSL[i].w;
    if (d2 > 6.25 * R2) continue;
    float ndl = max (dot (n, L * inversesqrt (d2 + 1e-3)), 0.0);
    c += (0.2 + 0.8 * ndl) / (1.0 + d2 / R2) * (1.0 - smoothstep (1.5 * R2, 6.25 * R2, d2));
  }
  return c * uSLC;
}
// light on a matte surface (no sky: what is not lit by the Sun, Jupiter or the lava is black)
vec3 lightAll (vec3 alb, vec3 n, vec3 p, float sh) {
  float ndl = max (dot (n, uSun), 0.0);
  vec3 sun = uSunCol * uSunK * ndl * sh;
  vec3 jup = uJupShine * (0.35 + 0.65 * max (dot (n, uJup), 0.0));
  return alb * (sun + jup + uAirglow * (0.6 + 0.4 * n.y) + lavaLight (p, n) + vec3 (0.75, 0.80, 0.90) * 0.22 * lampsAt (p) + siteLight (p, n));
}
