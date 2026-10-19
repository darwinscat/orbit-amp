uniform vec3 uCamR, uCamU, uCamF, uHR, uHF;
uniform vec2 uTan, uRes; uniform float uShear, uCamY, uTime, uSpeed, uDown, uFoam, uBoost;
uniform vec3 uAmb, uFoamC;
out vec3 vCol; out float vA;
#include "titan/hash.glsl"
#include "titan/capsule.glsl"

const float G = 1.35;                                // Titan's gravity
#include "titan/topx.glsl"
vec3 posAt(vec3 p0, vec3 v0, vec3 vt, float a, float k){ return p0 + vt * a + (v0 - vt) * ((1.0 - exp(-k * a)) / k); }
void main(){
  int id = gl_VertexID / 6; int cv = gl_VertexID - id * 6;
  uint u = uint(id);
  float h1 = h01(u, 21u), h2 = h01(u, 22u), h3 = h01(u, 23u), h4 = h01(u, 24u), h5 = h01(u, 25u), h6 = h01(u, 26u), h7 = h01(u, 27u), h8 = h01(u, 28u), h9 = h01(u, 29u);
  gl_Position = vec4(2.0, 2.0, 2.0, 1.0); vCol = vec3(0.0); vA = 0.0; vLoc = vec2(0.0); vLen = 0.0; vRad = 1.0; vSoft = 0.0;
  bool mist = h3 < 0.55;
  float amount = clamp(uDown * (mist ? 1.0 : 0.7) + uFoam * 0.9 + uBoost * 0.35, 0.0, 1.0);
  float gate = smoothstep(h9 - 0.08, h9, amount);    // more wash = more particles alive
  if (gate <= 0.0) return;
  float life = mist ? 2.4 + 2.2 * h1 : 1.4 + 1.2 * h1;
  float age = mod(uTime + h2 * life, life);
  // craft frame: x right, y up from the eye, z forward; kicked up off the liquid around the wash, mostly ahead
  float ang = (h5 - 0.5) * 2.8, r0 = mist ? 3.0 + 13.0 * h6 : 2.5 + 6.0 * h6;
  vec2 dir = vec2(sin(ang), cos(ang));
  vec3 p0 = vec3(dir.x * r0, -uCamY + 0.05, dir.y * r0);
  float sp = (mist ? 2.5 : 3.5) + 4.0 * h7;
  vec3 v0 = vec3(dir.x * sp, (mist ? 0.5 : 1.2) + (mist ? 1.0 : 2.4) * h8, dir.y * sp);
  float k = mist ? 1.8 : 0.9;                          // fine mist is held by the thick air
  vec3 vt = vec3(0.0, -G / k, -uSpeed);                // terminal: settles slowly, swept back by the air
  vec3 c = posAt(p0, v0, vt, age, k), cp = posAt(p0, v0, vt, max(age - 0.018, 0.0), k);
  if (c.y < -uCamY) return;                            // back in the sea
  vec3 P = uHR * c.x + vec3(0.0, c.y, 0.0) + uHF * c.z, Pp = uHR * cp.x + vec3(0.0, cp.y, 0.0) + uHF * cp.z;
  vec3 v0v = vec3(dot(P, uCamR), dot(P, uCamU), dot(P, uCamF)), v1v = vec3(dot(Pp, uCamR), dot(Pp, uCamU), dot(Pp, uCamF));
  if (v0v.z < 0.5 || v1v.z < 0.4) return;
  float r = mist ? 0.3 + 0.45 * h4 : 0.003 + 0.004 * h4;
  float focal = uRes.y / (2.0 * uTan.y);
  float rpx = r * focal / v0v.z;
  float rad = max(rpx, 0.7);
  float len = capsule(toPx(v1v), toPx(v0v), rad, cv, uRes);
  float fade = smoothstep(0.0, 0.3, age) * (1.0 - smoothstep(0.7, 1.0, age / life));
  vA = gate * fade * (mist ? 0.16 * smoothstep(2.0, 6.0, v0v.z) : 0.75 * min(1.0, sq(rpx / rad) * 1.5)) * mix(1.0, (2.0 * rad) / (2.0 * rad + len), 0.7);
  vCol = mix(uAmb * (mist ? 1.05 : 0.9), uFoamC * 0.9, clamp(uFoam, 0.0, 1.0));
  vSoft = mist ? 1.0 : 0.0;
}
