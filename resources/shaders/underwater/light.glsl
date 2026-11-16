
uniform sampler2D uCaustic;         // R, G: two Voronoi filament patterns; B: smooth warp; A: soft blobs (shafts)
uniform vec3 uLight;                // towards the light: up, slanted
uniform vec3 uShaftA, uShaftB;      // the basis of the plane the light is perpendicular to
uniform vec3 uFogLevel, uFogDeep, uFogUp, uSunGlow, uShaftCol;
uniform float uFogK, uSilt, uCauGain;
uniform vec2 uCauOff1, uCauOff2, uCauOffW, uShaftOff1, uShaftOff2;
uniform vec3 uLampPos0, uLampPos1, uLampDir;
uniform float uLampOn;
// The answer to the playing (16.11): the last attack's age (s) and strength. The living things light: a flash
// (0.25 s) and a slower glow (1.5 s), running out from the vehicle at 150 m/s: all in view within a fraction of a
// second, on the beat, with a hint of a ripple from the near ones to the far (Oleh, 16.11: 30 m/s read as one by one).
// Long after an attack it is exactly 1: a silent frame is the frame the world always was.
uniform vec2 uHitT;
float hitK (float dist) {
  float t = uHitT.x - dist / 150.0;
  if (t < 0.0) return 1.0;
  return 1.0 + uHitT.y * (2.4 * exp (-t / 0.25) + 1.1 * exp (-t / 1.5));
}
vec3 waterColour (vec3 V) {
  vec3 c = mix (uFogLevel, uFogDeep, smoothstep (0.0, -0.5, V.y));
  c = mix (c, uFogUp, smoothstep (-0.05, 0.5, V.y));
  float s = max (dot (V, uLight), 0.0);
  c += uSunGlow * (pow (s, 3.0) * 0.5 + s * s * 0.2);
  return c;
}
// Silt kicked up near the rover thickens and browns the water close by; then the ordinary blue-green fog.
vec3 applyFog (vec3 col, float dist, vec3 V) {
  float fs = uSilt * 0.32 * (1.0 - exp (-dist / 7.0)) * (0.35 + 0.65 * smoothstep (0.25, -0.10, V.y));   // the silt lies low, and light
  vec3 siltC = mix (uFogLevel, vec3 (0.46, 0.40, 0.36), 0.55) * 0.95;
  col = mix (col, siltC, fs);
  float f = 1.0 - exp (-dist * uFogK * (1.0 + 0.35 * uSilt));
  return mix (col, waterColour (V), f);
}
float fogAmount (float dist) { return 1.0 - exp (-dist * uFogK * (1.0 + 0.35 * uSilt)); }
// The caustic net on the floor at world xz (relative to the cell): two scrolling filament layers, multiplied.
float caustic (vec2 p) {
  vec2 w = texture (uCaustic, uCauOffW + p / 19.0 + uTime * vec2 (0.011, 0.006)).bb;
  w = (w - 0.5) * 0.45;
  float c1 = texture (uCaustic, uCauOff1 + p / 2.1 + uTime * vec2 (0.045, 0.028) + w).r;
  float c2 = texture (uCaustic, uCauOff2 + p / 1.44 - uTime * vec2 (0.030, 0.050) - w * 0.8).g;
  return min (c1 * c2 * uCauGain, 3.0);
}
// The light shafts: columns parallel to the light, a 2D pattern in the plane perpendicular to it.
float shafts (vec3 p) {
  vec2 s = vec2 (dot (p, uShaftA), dot (p, uShaftB));
  float n1 = texture (uCaustic, uShaftOff1 + s / 16.0 + uTime * vec2 (0.012, 0.004)).a;
  float n2 = texture (uCaustic, uShaftOff2 + s / 6.5 - uTime * vec2 (0.006, 0.016)).a;
  float n = n1 * 0.65 + n2 * 0.35;
  return 0.25 + 0.75 * pow (smoothstep (0.30, 0.90, n), 1.3);
}
// The rays: a 1D pattern by the angle around the light; every column projects to a line through the light.
// Every term wraps whole: phi by an integer count of the texture's period (atan's seam leaves no line), the drifts
// as fracts of their own on the CPU (a drift scaled after its fract jumps when it wraps), fixed lods (no seam mips).
uniform float uRayDrift, uRayDrift2;
float rays (vec3 dir) {
  vec3 q = dir - uLight * dot (dir, uLight); float ql = max (length (q), 1.0e-4);
  vec2 qa = vec2 (dot (q, uShaftA), dot (q, uShaftB)) / ql;
  float phi = atan (qa.y, qa.x) * (1.0 / 6.2831853);
  float fil = textureLod (uCaustic, vec2 (phi * 2.0 + uRayDrift, 0.41), 2.3).r;
  float band = textureLod (uCaustic, vec2 (phi + uRayDrift2 + uTime * 0.002, 0.63), 1.5).a;
  return (0.25 + 0.75 * fil) * (0.3 + 0.7 * band);
}
// The rover's two lamps on a surface: rel = the point relative to the camera.
vec3 lamps (vec3 n, vec3 rel) {
  vec3 sum = vec3 (0.0);
  for (int i = 0; i < 2; ++i) {
    vec3 d = rel - (i == 0 ? uLampPos0 : uLampPos1);
    float L = max (length (d), 0.05); vec3 dn = d / L;
    float cone = pow (max (dot (dn, uLampDir), 0.0), 16.0);
    float ndl = max (dot (n, -dn), 0.0);
    sum += vec3 (1.0, 0.92, 0.78) * 9.0 * cone * ndl / (1.0 + L * L * 0.09) * exp (-L * uFogK * 1.5);
  }
  return sum * uLampOn;
}
// The floor's and the rocks' light: ambient from the water, the sun through the caustic net, the lamps.
vec3 shadeGround (vec3 alb, vec3 n, vec3 pos, vec3 rel, float dist) {
  float ndl = max (dot (n, uLight), 0.0);
  float cFade = exp (-dist / 24.0);
  float caus = mix (1.0, caustic (pos.xz), 0.8 * cFade);
  vec3 sun = vec3 (0.72, 0.96, 0.86) * 1.35 * (0.55 + 0.45 * caus) * (1.0 - 0.18 * uSilt);
  vec3 amb = vec3 (0.09, 0.25, 0.30) * (0.55 + 0.45 * n.y);
  return alb * (amb + sun * ndl + lamps (n, rel));
}
