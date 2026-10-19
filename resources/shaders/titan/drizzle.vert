uniform vec3 uCamR, uCamU, uCamF, uCamRp, uCamUp, uCamFp;
uniform vec2 uTan, uRes; uniform float uShear, uCamY, uTime, uExpo;
uniform vec2 uBoxBase, uBoxCen, uWind;
uniform vec3 uVel;
uniform vec3 uAmb;
uniform float uDown;
out vec3 vCol; out float vA;
#include "titan/hash.glsl"
#include "titan/capsule.glsl"
#include <drizzle>
#include "titan/topx.glsl"
void main(){
  int id = gl_VertexID / 6; int cv = gl_VertexID - id * 6;
  uint u = uint(id);
  float h1 = h01(u, 11u), h2 = h01(u, 12u), h3 = h01(u, 13u), h4 = h01(u, 14u), h5 = h01(u, 15u), h6 = h01(u, 16u);
  vec2 xz = mod(vec2(h1, h2) * B - uBoxBase, B) - 0.5 * B + uBoxCen;
  float vf = 0.85 + 0.55 * h5;                       // big drops in thick air and 1/7 g: they drift down
  float y = mod(h3 * HB - vf * uTime, HB) - uCamY;   // from the sea surface up
  float sw = 6.2831853 * h6, fq = 0.5 + 0.5 * h4;
  vec2 sway = 0.14 * vec2(sin(uTime * fq * 3.0 + sw), cos(uTime * fq * 2.3 + sw));
  vec2 swayV = 0.14 * vec2(cos(uTime * fq * 3.0 + sw) * fq * 3.0, -sin(uTime * fq * 2.3 + sw) * fq * 2.3);
  vec3 P = vec3(xz.x + sway.x, y, xz.y + sway.y);
  float hd = length(P.xz), push = uDown * max(0.0, 1.0 - hd / 9.0);            // the downwash blows the drops aside
  P.xz += (P.xz / max(hd, 0.1)) * (push * 2.2); P.y -= push * 1.2;
  vec3 Pp = P - (vec3(uWind.x + swayV.x, -vf, uWind.y + swayV.y) - uVel) * uExpo;
  vec3 v0 = vec3(dot(P, uCamR), dot(P, uCamU), dot(P, uCamF));
  vec3 v1 = vec3(dot(Pp, uCamRp), dot(Pp, uCamUp), dot(Pp, uCamFp));
  gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vCol = vec3(0.0); vA = 0.0; vLoc = vec2(0.0); vLen = 0.0; vRad = 1.0; vSoft = 0.0;
  if (v0.z < 0.6 || v1.z < 0.4) return;
  float r = 0.0025 + 0.002 * h4;                      // 5–9 mm across: peas
  float focal = uRes.y / (2.0 * uTan.y);
  float rpx = r * focal / v0.z;
  float rad = max(rpx, 0.7);
  float len = capsule(toPx(v1), toPx(v0), rad, cv, uRes);
  float fadeZ = smoothstep(0.6, 2.0, v0.z) * (1.0 - smoothstep(30.0, 46.0, v0.z));
  vA = 0.75 * min(1.0, sq(rpx / rad) * 1.5) * fadeZ * mix(1.0, (2.0 * rad) / (2.0 * rad + len), 0.85);
  vCol = uAmb * 0.8;
  vCol = mix(vCol, uAmb, 1.0 - exp(-v0.z / 300.0));
}
