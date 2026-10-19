#include "plains/light.glsl"

uniform vec3 uCamFwd, uCamRight, uCamUp; uniform vec2 uTanHalf; uniform float uShear, uSunInt, uTime;
in vec2 vNdc;
out vec4 fragColor;
void main() {
  vec3 dir = normalize (uCamFwd + uCamRight * (vNdc.x * uTanHalf.x) + uCamUp * ((vNdc.y - uShear) * uTanHalf.y));
  float t = clamp (dir.y, 0.0, 1.0);
  vec3 col = mix (uHorizon, uZenith, pow (t, 0.55));
  float s = max (dot (dir, uSun), 0.0);
  col += uSunGlow * (pow (s, 8.0) * 0.55 + s * s * 0.14);
  col += vec3 (1.0, 0.97, 0.90) * uSunInt * (smoothstep (0.99993, 0.99998, s) + 0.25 * pow (s, 400.0));
  // Phobos and Deimos: two pale specks drifting slowly.
  vec3 m1 = normalize (vec3 (-0.55, 0.42 + 0.02 * sin (uTime * 0.01), -0.72));
  vec3 m2 = normalize (vec3 (0.62, 0.55, -0.55 + 0.01 * sin (uTime * 0.013)));
  col += vec3 (0.95, 0.9, 0.85) * (0.9 * smoothstep (0.999985, 0.999995, dot (dir, m1)) + 0.5 * smoothstep (0.999994, 0.999998, dot (dir, m2))) * (1.0 - uStorm);
  fragColor = vec4 (col, 1.0);
}
