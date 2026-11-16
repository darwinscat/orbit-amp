
uniform sampler2D uHeight;
uniform vec2 uCamCell;              // the integer world xz the frame is relative to (exact in float)
uniform vec2 uCamFrac;              // the camera's xz minus uCamCell
uniform vec2 uOff0, uOff1, uOff2;   // fract (uCamCell / L_i), from doubles
uniform vec3 uCamRel;               // the camera, relative to uCamCell
uniform float uTime;
const float L0 = 420.0, L1 = 96.0, L2 = 27.0;
uint ih (uint x) { x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
float hcell (ivec2 c, uint s) { return float (ih (uint (c.x) * 0x9E3779B1u ^ ih (uint (c.y) * 0x85EBCA77u ^ s))) * (1.0 / 4294967296.0); }
float hs (uint i, uint s) { return float (ih (i * 0x9E3779B1u ^ ih (s * 0x85EBCA77u))) * (1.0 / 4294967296.0); }
float sq (float x) { return x * x; }
// pr = world xz relative to uCamCell, metres. a: the 420 m layer, b: 96 m, c: 27 m.
float floorH (vec2 pr, out vec4 a, out vec4 b, out vec4 c) {
  a = HTEX (uOff0 + pr / L0);
  b = HTEX (uOff1 + pr / L1);
  c = HTEX (uOff2 + pr / L2);
  float swell  = (a.r - 0.5) * 10.0;
  float dunes  = (b.b - 0.5) * 2.6;
  float mounds = smoothstep (0.55, 0.95, c.a) * 1.8 * smoothstep (0.45, 0.75, a.g);
  float fine   = (c.r - 0.5) * 0.25;
  return swell + dunes + mounds + fine;
}
float floorH (vec2 pr) { vec4 a, b, c; return floorH (pr, a, b, c); }
