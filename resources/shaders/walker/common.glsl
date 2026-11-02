
uniform sampler2D uHeight;          // R height (m), G grove density, B moss
uniform vec2 uCamXZ;                // the eye's world xz
uniform float uCamY;                // the eye's height (absolute)
const float HP = 1024.0;            // the heightfield's period (m)
const float HTEX = 512.0;
uint ih (uint x) { x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
float hcell (ivec2 c, uint s) { return float (ih (uint (c.x) * 0x9E3779B1u ^ ih (uint (c.y) * 0x85EBCA77u ^ s))) * (1.0 / 4294967296.0); }
float hs (uint i, uint s) { return float (ih (i * 0x9E3779B1u ^ ih (s * 0x85EBCA77u))) * (1.0 / 4294967296.0); }
vec4 groundAt0 (vec2 rel) { return textureLod (uHeight, (uCamXZ + rel) / HP, 0.0); }
vec3 palette (float h) {
  int i = int (h + 0.5);
  if (i == 0) return vec3 (0.10, 0.95, 0.72);
  if (i == 1) return vec3 (0.22, 0.72, 1.00);
  if (i == 2) return vec3 (0.66, 0.36, 1.00);
  return vec3 (1.00, 0.30, 0.72);
}
uniform float uTime, uBreath, uStorm, uHit, uHitGlow;
// The glow of a cap: its own strength g; the bass's breath (slow, like lungs); TURBO's flare; an attack's flash on
// the caps near the walker and its slower, fainter glow on all of them. In silence: g * 0.55, what the forest is.
float glowOf (float g, float phase, float dist) {
  float near = smoothstep (60.0, 8.0, dist);
  return g * (0.68 + 0.75 * uBreath + 0.40 * uStorm + 0.18 * uHitGlow + 1.3 * uHit * near);
}
