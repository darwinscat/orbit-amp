
uniform vec3 uSun, uHorizon, uZenith, uSunGlow;
uniform float uHazeK, uStorm;
vec3 applyHaze (vec3 col, float dist, vec3 V) {
  float f = 1.0 - exp (-dist * uHazeK);
  f = max (f, smoothstep (11500.0, 15800.0, dist));
  float s = max (dot (V, uSun), 0.0);
  vec3 hz = uHorizon + uSunGlow * (pow (s, 8.0) * 0.55 + s * s * 0.14);
  return mix (col, hz, f);
}
vec3 shade (vec3 alb, vec3 n) {
  float ndl = max (dot (n, uSun), 0.0);
  vec3 sunC = vec3 (1.0, 0.86, 0.70) * (1.3 - 0.45 * uStorm);
  vec3 amb = mix (uHorizon, uZenith, 0.5) * (0.5 + 0.25 * uStorm) * (0.55 + 0.45 * n.y);
  return alb * (amb + sunC * ndl);
}
