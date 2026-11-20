// From the prototype's metal-shaders.js (COMMON), its body as it is.
uniform sampler2D uBase;            // the rogue planet's base: R swell, G ridged crests, B rough, A hot regions (RGBA16F)
uniform vec2 uCamCell;              // the integer world xz the frame is relative to (exact in float)
uniform vec2 uCamFrac;              // the camera's xz minus uCamCell
uniform vec2 uOff0, uOff1, uOff2;   // fract (uCamCell / L_i) (+ the layers' fixed offsets), from doubles
uniform vec3 uCamRel;               // the camera, relative to uCamCell
uniform float uTime;
uniform sampler2D uRoad;            // the road map: R the signed distance from the centreline (+ right), G the road's height
uniform vec2 uRoadOrg;              // its corner (rel)
uniform vec2 uLoopC;                // the loop's centre (rel)
uniform vec4 uCragA[6], uCragB[6];  // the crags: a capsule from a.xy to a.zw (rel), radius and height in b.xy
const float ROAD_SPAN = 3200.0, ROAD_HALF = 4.0;
const float L0 = 2400.0, L1 = 300.0, L2 = 48.0;
const float VENT_CELL = 48.0; const int VENT_M = 1000; const float VENT_PROB = 0.22;
const float TAU = 6.2831853;
uint ih (uint x) { x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
float hcell (ivec2 c, uint s) { return float (ih (uint (c.x) * 0x9E3779B1u ^ ih (uint (c.y) * 0x85EBCA77u ^ s))) * (1.0 / 4294967296.0); }
float hs (uint i, uint s) { return float (ih (i * 0x9E3779B1u ^ ih (s * 0x85EBCA77u))) * (1.0 / 4294967296.0); }
float sq (float x) { return x * x; }
vec2 roadDist (vec2 pr) { return RTEX ((pr - uRoadOrg) / ROAD_SPAN).rg; }
float cragH (vec2 pr) {
  float h = 0.0;
  for (int i = 0; i < 6; ++i) {
    if (uCragB[i].x <= 0.0) continue;
    vec2 a = uCragA[i].xy, e = uCragA[i].zw - a;
    float ee = dot (e, e);
    float t = ee > 0.0 ? clamp (dot (pr - a, e) / ee, 0.0, 1.0) : 0.0;
    float d = length (pr - a - e * t);
    if (d < uCragB[i].x) h += uCragB[i].y * (1.0 - smoothstep (0.4, 1.0, d / uCragB[i].x));
  }
  return h;
}
// A vent of the unwrapped 48 m cell cij (absolute): its centre (absolute) and its breathing phase.
bool ventAt (ivec2 cij, out vec2 ctr, out float phase) {
  ivec2 c = (cij + VENT_M * 64) % VENT_M;
  if (hcell (c, 1u) > VENT_PROB) return false;
  ctr = (vec2 (cij) + 0.2 + 0.6 * vec2 (hcell (c, 2u), hcell (c, 3u))) * VENT_CELL;
  phase = hcell (c, 4u);
  return true;
}
float breath (float t, float phase) { float b = 0.5 + 0.5 * sin (t * 0.8 + phase * 6.2831853); return b * b * b; }
float ventMound (vec2 pr, float ad) {
  vec2 p = pr + uCamCell, ctr; float ph;
  if (! ventAt (ivec2 (floor (p / VENT_CELL)), ctr, ph)) return 0.0;
  float d = length (p - ctr);
  if (d > 8.0) return 0.0;
  return (0.6 * exp (-sq (d / 2.6)) - 0.1 * exp (-d * d)) * smoothstep (12.0, 16.0, ad);
}
// The rogue planet's height (pr = world xz relative to uCamCell): the swell, the ridged ranges where it is cold, the
// hills, the lumps; the crags.
float terrainH (vec2 pr, out vec4 a, out vec4 b, out vec4 c) {
  a = BTEX (uOff0 + pr / L0);
  b = BTEX (uOff1 + pr / L1);
  c = BTEX (uOff2 + pr / L2);
  float mtnM = 1.0 - smoothstep (0.16, 0.34, a.a);
  float mtn = mtnM * mtnM * 160.0 * pow (b.g, 1.6);
  float swell = (a.r - 0.5) * 60.0;
  float hills = (b.r - 0.5) * 7.0 * (1.0 - 0.6 * mtnM);
  float fine = (c.b - 0.5) * 1.1 + (c.r - 0.5) * 0.4;
  return swell + mtn + hills + fine + cragH (pr);
}
// The ground: the terrain flattened along the road (the roadbed level, a shallow gutter, then the slopes); d the
// signed distance from the centreline.
float floorH (vec2 pr, out vec4 a, out vec4 b, out vec4 c, out float d) {
  vec2 rd = roadDist (pr); d = rd.x;
  float ad = abs (d);
  float tH = terrainH (pr, a, b, c) + ventMound (pr, ad);
  float w = smoothstep (ROAD_HALF + 2.5, 55.0, ad);
  return mix (rd.y, tH, w) - 0.15 * smoothstep (ROAD_HALF, ROAD_HALF + 1.2, ad) * (1.0 - smoothstep (ROAD_HALF + 2.0, ROAD_HALF + 5.0, ad));
}
float floorH (vec2 pr) { vec4 a, b, c; float d; return floorH (pr, a, b, c, d); }
