#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define LTEX(uv) textureLod(uLand, uv, 0.0)
#include "california/common.glsl"
#include "california/light.glsl"

in vec4 aB0;     // the box's centre (rel xyz), its yaw (its front, +z, turned by it toward the oncoming van)
in vec4 aB1;     // its half sizes (across, up, through), its part (0 wood, 1 a board with a face, 2 a banner's cloth)
in vec4 aB2;     // the face's rect in the atlas (u0, v0 top, u1, v1 bottom); u0 < 0: a painted placeholder
in vec4 aB3;     // the sun's visibility there (the hills), a flutter phase, the cloth's way (0 a banner, 1 a flag)
uniform mat4 uViewProj;
uniform vec2 uClothGrid;   // a cloth's cells across and down
out vec3 vRel; out vec3 vN; out vec2 vQ; out float vPart; out float vFace; out vec4 vRect; out float vSun; out float vCloth;
void main() {
  vCloth = aB3.z;
  vec3 n, p;
  vec2 q;
  int f;
  if (aB1.w > 1.5 && aB1.w < 2.5) {
    // a cloth: a banner hung from a rope across the road (sagging in the middle, a slow wave through it), or a flag
    // hung from a fence's top rail by its top edge, its free end blown out by the breeze and fluttering
    int gx = int (uClothGrid.x), gy = int (uClothGrid.y);
    int cell = gl_VertexID / 6, k = gl_VertexID % 6;
    vec2 o = vec2 ((k == 1 || k == 2 || k == 4) ? 1.0 : 0.0, (k == 2 || k == 4 || k == 5) ? 1.0 : 0.0);
    q = vec2 ((float (cell % gx) + o.x) / uClothGrid.x, (float (cell / gx) + o.y) / uClothGrid.y);
    float x = q.x * 2.0 - 1.0;
    float ph = uTime * 1.3 + aB3.y * 6.2831853;
    float wave, dw;
    if (aB3.z < 0.5) {
      wave = (0.10 * sin (x * 6.0 + ph) + 0.05 * sin (x * 2.7 - uTime * 0.8)) * (0.35 + 0.65 * (1.0 - q.y));
      dw = (0.60 * cos (x * 6.0 + ph) + 0.135 * cos (x * 2.7 - uTime * 0.8)) * (0.35 + 0.65 * (1.0 - q.y));
      p = vec3 (x, q.y * 2.0 - 1.0 - 0.10 * (1.0 - x * x) / max (aB1.y, 0.1), wave);
    } else {
      float cy = cos (aB0.w), sy = sin (aB0.w);
      float wn = dot (uWind, vec2 (sy, cy)) / 2.4;                 // the breeze across the flag
      float free = 1.0 - q.y, f2 = free * free;
      float flut = sin (x * 3.0 + uTime * 7.0 + ph * 3.0) * 0.5 + sin (uTime * 4.3 + ph * 5.0) * 0.5;
      wave = f2 * (0.16 * wn + 0.06 * flut);
      dw = f2 * 0.06 * 1.5 * cos (x * 3.0 + uTime * 7.0 + ph * 3.0);
      p = vec3 (x + 0.04 * f2 * flut, q.y * 2.0 - 1.0 + 0.15 * f2 * abs (wn) / max (aB1.y, 0.05), wave);
    }
    n = normalize (vec3 (-dw / max (aB1.x, 0.1), 0.0, 1.0));
    f = 0;
    vFace = 2.0;
  } else {
    // a box: 6 faces; a board with a face shows only its front and its back (its alpha cuts the shape)
    f = gl_VertexID / 6; int k = gl_VertexID % 6;
    if (gl_VertexID >= 36 || (aB1.w > 0.5 && aB1.w < 2.5 && f >= 2)) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vRel = vec3 (1.0); vN = vec3 (0.0, 0.0, 1.0); vQ = vec2 (0.0); vPart = 0.0; vFace = 0.0; vRect = vec4 (0.0); vSun = 0.0; return; }
    int c = (k == 0 || k == 3) ? 0 : (k == 1) ? 1 : (k == 2 || k == 4) ? 2 : 3;      // two triangles: 0 1 2, 0 2 3
    vec2 qq = vec2 ((c == 1 || c == 2) ? 1.0 : -1.0, (c >= 2) ? 1.0 : -1.0);
    if (f == 0)      { n = vec3 (0.0, 0.0, 1.0);  p = vec3 (qq.x, qq.y, 1.0); }
    else if (f == 1) { n = vec3 (0.0, 0.0, -1.0); p = vec3 (-qq.x, qq.y, -1.0); }
    else if (f == 2) { n = vec3 (1.0, 0.0, 0.0);  p = vec3 (1.0, qq.y, -qq.x); }
    else if (f == 3) { n = vec3 (-1.0, 0.0, 0.0); p = vec3 (-1.0, qq.y, qq.x); }
    else if (f == 4) { n = vec3 (0.0, 1.0, 0.0);  p = vec3 (qq.x, 1.0, -qq.y); }
    else             { n = vec3 (0.0, -1.0, 0.0); p = vec3 (qq.x, -1.0, qq.y); }
    q = (f <= 1) ? vec2 (p.x, p.y) * 0.5 + 0.5 : vec2 (0.5);
    vFace = (f == 0) ? 1.0 : (f == 1) ? -1.0 : 0.0;
  }
  vec3 lp = p * vec3 (aB1.x, aB1.y, max (aB1.z, 1.0e-3));
  if (aB1.w > 1.5 && aB1.w < 2.5) lp.z = p.z;
  float cy = cos (aB0.w), sy = sin (aB0.w);
  vec3 wp = aB0.xyz + vec3 (cy * lp.x + sy * lp.z, lp.y, -sy * lp.x + cy * lp.z);
  vN = vec3 (cy * n.x + sy * n.z, n.y, -sy * n.x + cy * n.z);
  vRel = wp - uCamRel; vPart = aB1.w; vRect = aB2; vQ = q; vSun = aB3.x;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
