#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define LTEX(uv) textureLod(uLand, uv, 0.0)
#include "california/common.glsl"
#include "california/light.glsl"

uniform mat4 uViewProj; uniform vec3 uCamR, uCamU, uCamF;
uniform float uAmount, uKind, uSizeK;
uniform vec3 uBox, uWrapOff, uFlowAcc;    // the box; fract (camera / box) in doubles; the flows (box units)
uniform float uBoxLift;
uniform vec4 uBurst[4];                   // per slot: its origin (rel xyz), its age (s)
uniform vec4 uBurstF[4];                  // its forward (xz), its level, its seed
uniform vec4 uFlock;                      // the flock's centre (rel xyz), its visibility
uniform vec4 uFlockDir;                   // its heading (xz), its wingbeat phase
uniform vec4 uBeamP;                      // a saucer's beam: its foot (rel xyz), its strength
uniform vec4 uBeamQ;                      // its radius on the ground, how high the petals rise, its age (s)
out vec2 vUv; out float vA; out vec3 vCol; out float vK;
void cullP () { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vA = 0.0; vUv = vec2 (0.0); vCol = vec3 (0.0); vK = 0.0; }
void main() {
  int id = gl_VertexID; uint pi = uint (id / 6); int ci = id - int (pi) * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  float r1 = hs (pi, 1u), r2 = hs (pi, 2u), r3 = hs (pi, 3u), r4 = hs (pi, 4u), r5 = hs (pi, 5u);
  float size = uSizeK * (0.5 + r4);
  vec3 P, rel; vec3 ax = uCamR, ay = uCamU;
  float alpha = 1.0;
  vK = 0.0;
  if (uKind < 1.5) {
    if (r5 > uAmount) { cullP (); return; }
    float fac = 0.6 + floor (r4 * 52.0) / 64.0;      // a mote's own pace: a multiple of 1/64, so a wrap at 4096 is seamless
    vec3 wob = vec3 (sin (uTime * 0.8 + r4 * 40.0), 0.5 * sin (uTime * 0.6 + r3 * 30.0), cos (uTime * 0.7 + r2 * 50.0)) * (uKind < 0.5 ? 0.25 : 0.7);
    vec3 q = fract (vec3 (r1, r2, r3) + uFlowAcc * fac - uWrapOff + wob / uBox) - 0.5;
    rel = q * uBox + vec3 (0.0, uBoxLift, 0.0);
    P = uCamRel + rel;
    float gy = floorH (P.xz);
    if (P.y < gy + 0.05) { cullP (); return; }
    float back = pow (max (dot (normalize (rel), uSun), 0.0), 8.0);
    float lit = shadowAt (P.xz).x * hillSun (P.xz).x;
    if (uKind < 0.5) { vCol = vec3 (1.0, 0.82, 0.45) * (0.30 + 3.2 * back * lit); alpha = 0.55; }
    else { vCol = mix (vec3 (0.92, 0.90, 0.86), vec3 (1.0, 0.86, 0.6), back) * (0.45 + 2.4 * back * lit); alpha = 0.8; vK = 1.0; }
  } else if (uKind < 2.5) {
    int slot = int (pi / 150u);
    vec4 B = uBurst[slot], F = uBurstF[slot];
    float age = B.w;
    if (age > 4.5 || F.z < 0.01) { cullP (); return; }
    uint pj = pi + uint (F.w * 997.0);
    float q1 = hs (pj, 11u), q2 = hs (pj, 12u), q3 = hs (pj, 13u), q4 = hs (pj, 14u), q5 = hs (pj, 15u), q6 = hs (pj, 16u), q7 = hs (pj, 17u);
    vec2 fw = F.xy, rt = vec2 (-fw.y, fw.x);
    float side = q6 < 0.5 ? -1.0 : 1.0;
    vec2 p0 = B.xz + fw * (6.0 + 16.0 * q1) + rt * side * (5.2 + 7.0 * q2);
    float gy = floorH (p0) + 0.3;
    float tau = 0.38, up = (2.6 + 3.2 * q3) * (0.5 + 0.5 * F.z);
    float y = gy + up * tau * (1.0 - exp (-age / tau)) - 0.25 * age;
    vec2 drift = uWind * age * 0.6 + rt * side * age * 0.35 + 0.3 * vec2 (sin (age * 2.3 + q4 * 6.28), cos (age * 1.9 + q5 * 6.28));
    P = vec3 (p0.x + drift.x, max (y, gy - 0.1), p0.y + drift.y);
    rel = P - uCamRel;
    float spin = age * (4.0 + 6.0 * q4) + q5 * 6.28;
    ax = uCamR * (0.25 + 0.75 * abs (cos (spin))); ay = uCamU;
    size = 0.08 * (0.7 + 0.6 * q7);
    alpha = smoothstep (4.5, 3.0, age) * smoothstep (0.0, 0.06, age);
    vec3 pc = q7 < 0.8 ? vec3 (1.0, 0.45, 0.05) : vec3 (0.50, 0.38, 0.88);
    float back = pow (max (dot (normalize (rel), uSun), 0.0), 4.0);
    vCol = pc * (0.55 + 1.6 * back) * (1.0 + 1.3 * exp (-age / 0.4) * F.z);
    vK = 2.0;
  } else if (uKind > 3.5) {
    // petals lifting in a saucer's beam: from the poppies round its foot, turning slowly up the middle, fading high
    if (uBeamP.w < 0.01) { cullP (); return; }
    float q1 = hs (pi, 21u), q2 = hs (pi, 22u), q3 = hs (pi, 23u), q4 = hs (pi, 24u);
    float ph = fract (uBeamQ.z * (0.07 + 0.05 * q1) + q2);
    float rr = uBeamQ.x * (0.15 + 0.6 * q3) * (1.0 - 0.6 * ph);
    float ang = q4 * 6.2831853 + uBeamQ.z * (0.5 + 0.4 * q1) + ph * 3.0;
    P = uBeamP.xyz + vec3 (cos (ang) * rr, 0.3 + ph * uBeamQ.y, sin (ang) * rr);
    rel = P - uCamRel;
    float spin = uTime * (3.0 + 4.0 * q2) + q1 * 6.28;
    ax = uCamR * (0.25 + 0.75 * abs (cos (spin))); ay = uCamU;
    size = 0.11 * (0.7 + 0.6 * q3);
    alpha = uBeamP.w * smoothstep (0.0, 0.08, ph) * (1.0 - smoothstep (0.65, 1.0, ph));
    vCol = mix (vec3 (1.0, 0.45, 0.05), vec3 (0.75, 1.0, 0.85), 0.35 + 0.3 * ph) * 1.1;
    vK = 2.0;
  } else {
    if (uFlock.w < 0.01 || pi >= 11u) { cullP (); return; }
    vec2 fd = uFlockDir.xy, rt = vec2 (-fd.y, fd.x);
    float k = float (pi);
    float rank = floor ((k + 1.0) * 0.5), sd = (int (pi) % 2 == 0) ? 1.0 : -1.0;
    vec2 off = -fd * rank * 2.8 + rt * sd * rank * 2.2 + 0.9 * vec2 (sin (uTime * 0.31 + r1 * 6.28), cos (uTime * 0.27 + r2 * 6.28));
    P = uFlock.xyz + vec3 (off.x, 0.7 * sin (uTime * 0.4 + r3 * 6.28) + rank * 0.2, off.y);
    rel = P - uCamRel;
    size = 1.1;
    alpha = uFlock.w;
    vCol = mix (vec3 (0.10, 0.08, 0.10), hazeColour (normalize (rel)), hazeAmount (length (rel)) * 0.9);
    vK = 3.0 + fract ((uFlockDir.z + r4 * 6.28 + uTime * (5.0 + 1.5 * r5)) / 6.2831853);
  }
  float dist = length (rel);
  vec3 wp = P + (ax * (corner.x - 0.5) + ay * (corner.y - 0.5)) * size;
  gl_Position = uViewProj * vec4 (wp, 1.0);
  vUv = corner;
  vA = alpha * smoothstep (0.4, 1.2, dist) * (1.0 - hazeAmount (dist) * 0.7);
}
