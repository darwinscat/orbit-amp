// From the prototype's metal-shaders.js (VS_SIGN), its body as it is.
#include "metal/vtex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

in vec4 aB0;     // the centre (rel xyz; a banner: its top edge's middle), its yaw (its front, +z)
in vec4 aB1;     // half sizes (across, up, through), part (0 steel, 1 a board with a face, 2 cloth)
in vec4 aB2;     // the face's rect in its atlas (u0, v0 top, u1, v1 bottom); u0 < 0: a placeholder (-1 a banner, -2 a jacket)
in vec4 aB3;     // -, a flutter phase, the cloth's way (1 a flag, 2 a hanging banner), -
uniform mat4 uViewProj;
uniform vec2 uClothGrid;
uniform vec2 uWind;
out vec3 vRel; out vec3 vN; out vec2 vQ; out float vPart; out float vFace; out vec4 vRect; out float vCloth;
void main() {
  vCloth = aB3.z;
  vec3 n, p; vec2 q;
  if (aB1.w > 1.5) {
    int gx = int (uClothGrid.x);
    int cell = gl_VertexID / 6, k = gl_VertexID % 6;
    vec2 o = vec2 ((k == 1 || k == 2 || k == 4) ? 1.0 : 0.0, (k == 2 || k == 4 || k == 5) ? 1.0 : 0.0);
    q = vec2 ((float (cell % gx) + o.x) / uClothGrid.x, (float (cell / gx) + o.y) / uClothGrid.y);
    float x = q.x * 2.0 - 1.0, ph = uTime * 1.3 + aB3.y * 6.2831853;
    float free = 1.0 - q.y;                        // 0 at the top edge (hung there), 1 at the bottom / free end
    float wave, dw;
    if (aB3.z > 1.5) {
      // a banner hanging by its top: waves run down it, its free bottom swinging in the wind
      float w0 = sin (free * 4.0 - uTime * 2.1 + ph) * 0.5 + sin (x * 2.5 + uTime * 1.3 + ph * 2.0) * 0.3;
      wave = free * (0.35 + 0.25 * free) * w0 * aB1.x * 0.5;
      dw = free * 0.3 * cos (x * 2.5 + uTime * 1.3 + ph * 2.0) * 2.5 * 0.5;
      p = vec3 (x + 0.05 * free * sin (uTime * 1.7 + ph), -q.y * 2.0 + 1.0, wave / max (aB1.z, 1e-3));
      p.y = 1.0 - (1.0 - q.y) * 2.0;
      p.z = wave;
    } else {
      float flut = sin (x * 3.0 + uTime * 7.0 + ph * 3.0) * 0.5 + sin (uTime * 4.3 + ph * 5.0) * 0.5;
      float f2 = (x * 0.5 + 0.5); f2 *= f2;
      wave = f2 * (0.35 + 0.12 * flut);
      dw = f2 * 0.12 * 1.5 * cos (x * 3.0 + uTime * 7.0 + ph * 3.0);
      p = vec3 (x, q.y * 2.0 - 1.0 - 0.08 * f2, wave);
    }
    n = normalize (vec3 (-dw / max (aB1.x, 0.1), 0.0, 1.0));
    vFace = 2.0;
  } else {
    int f = gl_VertexID / 6, k = gl_VertexID % 6;
    if (gl_VertexID >= 36 || (aB1.w > 0.5 && f >= 2)) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vRel = vec3 (1.0); vN = vec3 (0.0, 0.0, 1.0); vQ = vec2 (0.0); vPart = 0.0; vFace = 0.0; vRect = vec4 (0.0); return; }
    int c = (k == 0 || k == 3) ? 0 : (k == 1) ? 1 : (k == 2 || k == 4) ? 2 : 3;
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
  if (aB1.w > 1.5) { lp.z = p.z; if (aB3.z > 1.5) lp.y = (p.y - 1.0) * aB1.y; }
  float cy = cos (aB0.w), sy = sin (aB0.w);
  vec3 wp = aB0.xyz + vec3 (cy * lp.x + sy * lp.z, lp.y, -sy * lp.x + cy * lp.z);
  vN = vec3 (cy * n.x + sy * n.z, n.y, -sy * n.x + cy * n.z);
  vRel = wp - uCamRel; vPart = aB1.w; vRect = aB2; vQ = q;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
