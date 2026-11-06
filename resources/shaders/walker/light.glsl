
uniform sampler2D uLight;           // the caps' glow on the ground, top-down around the eye
uniform float uLmSize, uLmScale;
uniform vec3 uSunDir, uSunCol, uSkyZen, uSkyHor, uFogCol, uSunGlow;
uniform float uFogK, uFogGlow;
vec3 lightAt (vec2 rel) { return texture (uLight, rel / uLmSize + 0.5).rgb * uLmScale; }
vec3 ambient (vec3 n) { return mix (uSkyHor, uSkyZen, 0.5 + 0.5 * n.y) * 0.55; }
vec3 sunLight (vec3 n) { return uSunCol * max (dot (n, uSunDir), 0.0); }
uniform vec3 uSkyHorSun;
vec3 horizonAt (vec3 V) {
  vec2 dxz = normalize (V.xz + vec2 (1e-5, 0.0)), sxz = normalize (uSunDir.xz);
  float az = 0.5 + 0.5 * dot (dxz, sxz);
  float s = max (dot (normalize (vec3 (V.x, 0.0, V.z)), uSunDir), 0.0);
  return mix (uSkyHor, uSkyHorSun, pow (az, 3.0)) + uSunGlow * pow (s, 20.0) * 0.45;
}
vec3 applyFog (vec3 col, float dist, vec3 V, vec3 lm) {
  float f = 1.0 - exp (-dist * uFogK);
  vec3 fc = mix (uFogCol, horizonAt (V) * 0.85, f * f) + lm * uFogGlow;
  return mix (col, fc, f);
}
float dither (vec2 px) { return fract (sin (dot (px, vec2 (12.9898, 78.233))) * 43758.5453) - 0.5; }
vec3 finish (vec3 c, vec2 px) { c = 1.0 - exp (-max (c, vec3 (0.0)) * 1.25); return c + dither (px) / 255.0; }
