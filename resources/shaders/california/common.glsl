
uniform sampler2D uHeight;
uniform vec2 uCamCell;              // the integer world xz the frame is relative to (exact in float)
uniform vec2 uCamFrac;              // the camera's xz minus uCamCell
uniform vec2 uOff0, uOff1, uOff2;   // fract (uCamCell / L_i), from doubles
uniform vec3 uCamRel;               // the camera, relative to uCamCell
uniform float uTime;
uniform vec4 uBeamPool;              // a saucer's beam on the ground: its centre (rel xz), radius, strength
float beamPool (vec2 pr) { return uBeamPool.w * (1.0 - smoothstep (0.15, 1.0, length (pr - uBeamPool.xy) / max (uBeamPool.z, 0.1))); }
uniform vec2 uLoopC;                // the road's loop: its centre, relative to uCamCell
uniform sampler2D uRoad;            // the road map (made once on the CPU): R the signed distance from the road's
uniform vec2 uRoadOrg;              // centreline (+ right of the way), G the road's own height there, B the sun's
const float ROAD_SPAN = 3200.0;     // visibility past the hills (the sun and the hills stand still); its corner (rel); its side (256 texels)
uniform sampler2D uLand;            // the land map (the same ground): RG the ground's normal (x, z), B the poppies' field, A the lupine's
const float L0 = 2100.0, L1 = 330.0, L2 = 48.0;           // the hills, the swells, the bumps
const float HILL_A = 70.0, MED_A = 16.0, FINE_A = 0.9;
// THE ROAD: a closed loop of about 5 km round a centre, its radius a function of the angle (four wobbles), so the
// distance to it is a radial gap and an angle — as cheap as a road along an axis, and it closes on itself.
const float LOOP_R = 760.0;
const vec4 LOOP_K = vec4 (2.0, 3.0, 5.0, 7.0), LOOP_A = vec4 (200.0, 40.0, 22.0, 10.0), LOOP_P = vec4 (2.8623, 2.1, 4.0, 1.3);
uint ih (uint x) { x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
float hcell (ivec2 c, uint s) { return float (ih (uint (c.x) * 0x9E3779B1u ^ ih (uint (c.y) * 0x85EBCA77u ^ s))) * (1.0 / 4294967296.0); }
float hs (uint i, uint s) { return float (ih (i * 0x9E3779B1u ^ ih (s * 0x85EBCA77u))) * (1.0 / 4294967296.0); }
float sq (float x) { return x * x; }
// The loop's radius at the angle th and its derivative (the CPU makes the road map from it; the shaders need it
// only for the centre line's dashes).
vec2 loopR (float th) { vec4 a = LOOP_K * th + LOOP_P; return vec2 (LOOP_R + dot (LOOP_A, cos (a)), -dot (LOOP_A * LOOP_K, sin (a))); }
// x: the signed distance from the centreline (+: right of the way, the loop's inside), y: the road's height there.
vec2 roadDist (vec2 pr) { return RTEX ((pr - uRoadOrg) / ROAD_SPAN).rg; }
// The land map at pr, and how far inside the map pr is (w: 1 inside).
vec4 landAt (vec2 pr) { return LTEX ((pr - uRoadOrg) / ROAD_SPAN); }
float inMap (vec2 pr) { vec2 uv = (pr - uRoadOrg) / ROAD_SPAN; vec2 e = min (uv, 1.0 - uv); return smoothstep (0.0, 0.03, min (e.x, e.y)); }
// The sun's visibility past the hills at pr (from the road map), and how far inside the map pr is (1 inside).
vec2 hillSun (vec2 pr) {
  vec2 uv = (pr - uRoadOrg) / ROAD_SPAN;
  vec2 e = min (uv, 1.0 - uv);
  return vec2 (RTEX (uv).b, smoothstep (0.0, 0.03, min (e.x, e.y)));
}
// pr = world xz relative to uCamCell, metres. a: the 2100 m layer, b: 330 m, c: 48 m; d: the signed road distance.
// The hills, flattened along the road (the road and its shoulders level across, a shallow ditch, then the slopes).
float floorH (vec2 pr, out vec4 a, out vec4 b, out vec4 c, out float d) {
  a = HTEX (uOff0 + pr / L0);
  b = HTEX (uOff1 + pr / L1);
  c = HTEX (uOff2 + pr / L2);
  vec2 rd = roadDist (pr); d = rd.x;
  float ad = abs (d);
  float hT = HILL_A * (a.r - 0.5) + MED_A * (b.b - 0.5);
  float w = smoothstep (6.0, 75.0, ad);
  float ditch = 0.35 * smoothstep (4.7, 5.6, ad) * (1.0 - smoothstep (6.2, 8.5, ad));
  return mix (rd.y, hT, w) + FINE_A * (c.a - 0.5) * smoothstep (4.8, 7.0, ad) - ditch;
}
float floorH (vec2 pr) { vec4 a, b, c; float d; return floorH (pr, a, b, c, d); }
// Where the flowers grow (the ground's colour far off and the instanced flowers near read the same fields).
float poppyField (vec4 a, vec4 b, float ad) {
  float f = smoothstep (0.42, 0.62, mix (b.g, a.b, 0.45));
  f = max (f, 0.8 * smoothstep (0.28, 0.52, b.g) * (1.0 - smoothstep (7.0, 18.0, ad)));   // the verges
  return f * smoothstep (4.9, 5.6, ad);
}
float lupineField (vec4 a, vec4 b, float ad) {
  return smoothstep (0.66, 0.80, b.r) * smoothstep (0.35, 0.65, a.g) * smoothstep (4.9, 5.6, ad);
}
// The poles' shadows (analytic: thin, sharp at the foot, soft and faint at the far end), near the road. The poles
// stand every ~42 m on the road's right, 7 m out; the CPU hands the eight nearest the van: uPoles[i] = the foot (rel
// xz), the shadow's length, the crossarm's shadow's distance; uPoleSide[i] = the crossarm's way (the road's right).
uniform vec4 uPoles[8]; uniform vec2 uPoleSide[8];
float poleShadow (vec2 pr, vec2 D) {
  float lit = 1.0;
  for (int i = 0; i < 8; ++i) {
    vec4 P = uPoles[i];
    vec2 q = pr - P.xy;
    float along = dot (q, D);
    if (along < -2.0 || along > P.z + 2.0) continue;
    float t = clamp (along, 0.0, P.z);
    float d = length (q - D * t);
    if (d > 3.0) continue;
    float w = 0.10 + 0.0048 * t;
    float s = (1.0 - smoothstep (w * 0.4, w * 1.6, d)) * (0.62 - 0.40 * t / P.z);
    vec2 arm = P.xy + D * P.w; vec2 side = uPoleSide[i];
    float u = clamp (dot (pr - arm, side), -1.0, 1.0);
    s = max (s, (1.0 - smoothstep (0.06, 0.45, length (pr - arm - side * u))) * 0.34);
    lit *= 1.0 - s;
  }
  return lit;
}
