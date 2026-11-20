// From the prototype's io-shaders.js (COMMON), its body as it is.

uniform sampler2D uBase;            // R swell, G the mesas' field, B rough, A the colour regions (RGBA16F)
uniform vec2 uCamCell;              // the integer world xz the frame is relative to (exact in float)
uniform vec2 uCamFrac;              // the camera's xz minus uCamCell
uniform vec2 uOff0, uOff1, uOff2;   // fract (uCamCell / L_i) (+ the layers' fixed offsets), from doubles
uniform vec3 uCamRel;               // the camera, relative to uCamCell
uniform float uTime;
uniform sampler2D uRoad;            // the road map: R the signed distance from the centreline (+ right), G the road's height
uniform vec2 uRoadOrg;              // its corner (rel)
uniform sampler2D uFeat;            // the feature map: R the flows' signed distance, G freshness, B red deposits, A white frost
uniform vec2 uFeatOrg;              // its corner (rel)
uniform vec2 uLoopC;                // the loop's centre (rel)
uniform vec4 uCaldA[12];            // the paterae: centre (rel xz), floor radius, wall width
uniform vec4 uCaldB[12];            // depth, rim height, outer radius, the plain's height at the centre
uniform vec4 uCaldC[12];            // lake radius, activity (0..1), -, -
uniform int uCaldN;
const float ROAD_SPAN = 3200.0, ROAD_HALF = 4.0, FEAT_SPAN = 4800.0;
const float L0 = 2400.0, L1 = 300.0, L2 = 48.0;
const float IO_R = 1821600.0;
const float TAU = 6.2831853;
uint ih (uint x) { x ^= x >> 16u; x *= 0x7feb352du; x ^= x >> 15u; x *= 0x846ca68bu; x ^= x >> 16u; return x; }
float hcell (ivec2 c, uint s) { return float (ih (uint (c.x) * 0x9E3779B1u ^ ih (uint (c.y) * 0x85EBCA77u ^ s))) * (1.0 / 4294967296.0); }
float hs (uint i, uint s) { return float (ih (i * 0x9E3779B1u ^ ih (s * 0x85EBCA77u))) * (1.0 / 4294967296.0); }
float sq (float x) { return x * x; }
vec2 roadDist (vec2 pr) { return RTEX ((pr - uRoadOrg) / ROAD_SPAN).rg; }
vec4 featAt (vec2 pr) { return FTEX ((pr - uFeatOrg) / FEAT_SPAN); }
// the patera whose reach this point is in (the road map's B names it, its index + 1; -1 none)
int calderaAt (vec2 pr) {
  ivec2 ti = ivec2 (floor ((pr - uRoadOrg) * (256.0 / ROAD_SPAN)));
  if (ti.x < 0 || ti.y < 0 || ti.x > 255 || ti.y > 255) return -1;
  return int (texelFetch (uRoad, ti, 0).b + 0.5) - 1;
}
// its part of the height: (height, how much it replaces the terrain, the mesas' push)
vec3 calderaField (vec2 pr) {
  int i = calderaAt (pr);
  if (i < 0) return vec3 (0.0);
  vec4 A = uCaldA[i], B = uCaldB[i];
  float d = length (pr - A.xy);
  float push = 0.16 * (1.0 - smoothstep (A.z + A.w, A.z + A.w + 160.0, d));
  if (d >= B.z) return vec3 (0.0, 0.0, push);
  if (d < A.z + A.w) return vec3 (B.w - B.x + (B.x + B.y) * smoothstep (A.z, A.z + A.w, d), 1.0, push);
  float r2 = (d - A.z - A.w) / (B.z - A.z - A.w);
  return vec3 (B.w + B.y * (1.0 - r2) * (1.0 - r2), 1.0 - smoothstep (0.0, 1.0, r2), push);
}
// Io's height (pr = world xz relative to uCamCell; ad the distance from the road's centreline): the plain's swell and
// hills, the layered mesas (two terraces, scalloped by the 300 m layer, kept back from the road and the paterae), the
// lumps, the flows' raised lobes, the paterae.
float terrainH (vec2 pr, float ad, out vec4 a, out vec4 b, out vec4 c, out vec4 f) {
  a = BTEX (uOff0 + pr / L0);
  b = BTEX (uOff1 + pr / L1);
  c = BTEX (uOff2 + pr / L2);
  f = featAt (pr);
  vec3 cf = calderaField (pr);
  float plain = (a.r - 0.5) * 26.0 + (b.r - 0.5) * 5.0;
  float m = a.g * 0.86 + b.g * 0.14 - 0.11 * (1.0 - smoothstep (30.0, 160.0, ad)) - cf.z;
  float mesa = (28.0 * smoothstep (0.684, 0.716, m) + 16.0 * smoothstep (0.768, 0.792, m)) * (0.9 + 0.2 * b.b);
  float fine = (c.b - 0.5) * 0.9 + (c.r - 0.5) * 0.35;
  float lobe = 1.5 * (1.0 - smoothstep (-5.0, 0.0, (f.r - 0.5) * 48.0));
  return mix (plain + mesa + fine + lobe, cf.x, cf.y);
}
// The ground: the terrain flattened along the road (the graded track, a shallow edge, then the plain); d the signed
// distance from the centreline.
float floorH (vec2 pr, out vec4 a, out vec4 b, out vec4 c, out vec4 f, out float d) {
  vec2 rd = roadDist (pr); d = rd.x;
  float ad = abs (d);
  float tH = terrainH (pr, ad, a, b, c, f);
  float w = smoothstep (ROAD_HALF + 2.5, 45.0, ad);
  return mix (rd.y, tH, w) - 0.10 * smoothstep (ROAD_HALF, ROAD_HALF + 1.2, ad) * (1.0 - smoothstep (ROAD_HALF + 2.0, ROAD_HALF + 5.0, ad));
}
float floorH (vec2 pr) { vec4 a, b, c, f; float d; return floorH (pr, a, b, c, f, d); }
// Io's curvature: how far below the camera's tangent plane a point at p (rel) lies
float curveDrop (vec3 p) { vec2 q = p.xz - uCamRel.xz; return dot (q, q) * (0.5 / IO_R); }
vec4 project (mat4 VP, vec3 p) { return VP * vec4 (p.x, p.y - curveDrop (p), p.z, 1.0); }
