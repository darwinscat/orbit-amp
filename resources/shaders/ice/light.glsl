
uniform vec3 uSun, uJup, uHorizon, uMist;
uniform float uHazeK, uSnow, uSunInt;
vec3 applyHaze (vec3 col, float dist, vec3 V) {
  float f = 1.0 - exp (-dist * uHazeK);
  f = max (f, smoothstep (6500.0, 8000.0, dist));
  return mix (col, uHorizon, f);
}
// Snow and ice: the sun (warm white, wrapped a little), the blue skylight off the ice itself, Jupiter's shine.
vec3 shadeSnow (vec3 alb, vec3 n) {
  float ndl = dot (n, uSun);
  float diff = max (ndl, 0.0) * 0.9 + 0.12 * smoothstep (-0.35, 0.35, ndl);
  vec3 sunC = vec3 (1.0, 0.96, 0.90) * uSunInt;
  vec3 amb = vec3 (0.26, 0.42, 0.74) * (0.32 + 0.12 * uSnow) * (0.55 + 0.45 * n.y);
  vec3 jup = vec3 (0.60, 0.48, 0.38) * 0.18 * max (dot (n, uJup), 0.0);
  return alb * (amb + jup + sunC * diff);
}
