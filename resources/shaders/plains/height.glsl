
uniform sampler2D uHeight;
uniform vec2 uCamCell;      // the integer world xz the frame is relative to (exact in float)
uniform vec2 uCamFrac;      // the camera's xz minus uCamCell
uniform vec2 uTexOff0, uTexOff1, uTexOff2;  // fract (uCamCell / L_i) (+ the fixed layer offsets), from doubles
const float L0 = 7000.0, L1 = 1250.0, L2 = 190.0;
const float CRC = 350.0;                    // the crater cell
const int   CRM = 1900;                     // WORLD / CRC
#include "common/ih.glsl"
#include "common/hcell.glsl"
float craters (vec2 p, float mask) {         // p = absolute world xz
  vec2 q = p / CRC; vec2 fc = floor (q);
  ivec2 c = (ivec2 (fc) + CRM * 64) % CRM;   // never negative, wraps with the world
  float r0 = hcell (c, 1u);
  if (r0 > 0.42 || mask < 0.01) return 0.0;
  vec2 ctr = (fc + 0.25 + 0.5 * vec2 (hcell (c, 2u), hcell (c, 3u))) * CRC;
  float k = hcell (c, 4u);
  float R = CRC * (0.045 + 0.16 * k * k);    // 16 .. 72 m radius
  float d = length (p - ctr) / R;
  if (d > 1.6) return 0.0;
  float bowl = -(1.0 - d * d) * step (d, 1.0);
  float rim = exp (-pow ((d - 1.0) / 0.22, 2.0));
  return mask * R * (0.24 * bowl + 0.065 * rim);
}
float terrainH (vec2 pr, out vec4 a, out vec4 b, out vec4 c) {   // pr = world xz relative to uCamCell, metres
  a = HTEX (uTexOff0 + pr / L0);
  b = HTEX (uTexOff1 + pr / L1);
  c = HTEX (uTexOff2 + pr / L2);
  float dist = length (pr - uCamFrac);
  float mtnMask = smoothstep (0.50, 0.92, a.r);
  float mtn = 430.0 * pow (mtnMask, 1.25) * (0.5 + 0.9 * b.b);
  float swell = (a.a - 0.5) * 90.0;
  float hills = (b.r - 0.5) * 48.0 * (0.4 + 0.6 * a.g);
  float plainM = 1.0 - mtnMask;
  float mesaM = smoothstep (0.60, 0.64, b.g) * smoothstep (0.35, 0.6, a.b) * plainM;
  float mesa = mesaM * 24.0 * (1.0 + 0.35 * (c.g - 0.5));
  float canyon = -smoothstep (0.80, 0.97, 1.0 - abs (2.0 * a.g - 1.0)) * 34.0 * plainM * smoothstep (0.45, 0.62, a.b);
  float fade = 1.0 - smoothstep (800.0, 2500.0, dist);
  float dune = ((c.r - 0.5) * 5.0 + (c.b - 0.5) * 1.3) * fade;
  float crm = plainM * (1.0 - mesaM) * smoothstep (0.90, 0.75, 1.0 - abs (2.0 * a.g - 1.0));
  return swell + mtn + hills + mesa + canyon + dune + craters (pr + uCamCell, crm);
}
float terrainH (vec2 pr) { vec4 a, b, c; return terrainH (pr, a, b, c); }
