#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

uniform mat4 uViewProj; uniform vec3 uCamR, uCamU, uCamF;
uniform float uAmount, uKind, uSizeK, uSpeed, uTanH;
uniform vec3 uBox, uWrapOff, uFlowAcc;    // the box; fract (camera / box) in doubles; the flows (box units / ages)
uniform vec2 uFwdLevel, uRightLevel;      // the rover's heading on the floor
uniform vec2 uThin;                       // at the foot: from which particle on, and how much of them is left
out vec2 vUv; out float vA; out vec3 vCol; out float vF;
void main() {
  int id = gl_VertexID; uint pi = uint (id / 6); int ci = id - int (pi) * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  float r1 = hs (pi, 1u), r2 = hs (pi, 2u), r3 = hs (pi, 3u), r4 = hs (pi, 4u), r5 = hs (pi, 5u);
  if (r5 > uAmount) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vA = 0.0; vUv = vec2 (0.0); vCol = vec3 (0.0); vF = 0.0; return; }
  float fac = 0.6 + floor (r4 * 52.0) / 64.0;        // a particle's own pace: a multiple of 1/64, so a wrap at 4096 is seamless
  vec3 rel, P;
  float size = uSizeK * (0.5 + r4);
  float thin = 1.0;
  if (uKind < 0.5) {                                 // the marine snow: anchored in the world, the camera wraps through its box
    vec3 wob = vec3 (sin (uTime * 0.7 + r4 * 40.0), cos (uTime * 0.5 + r3 * 30.0), sin (uTime * 0.6 + r2 * 50.0)) * 0.3;
    vec3 q = fract (vec3 (r1, r2, r3) + uFlowAcc * (0.6 + 0.8 * r4) - uWrapOff + wob / uBox) - 0.5;
    rel = q * uBox; P = uCamRel + rel;
  } else if (uKind < 1.5) {                          // the silt: on the floor ahead, across the whole view, streaming back, billowing up
    float z = 0.5 + fract (r3 + uFlowAcc.z * fac) * uBox.z;
    float x = (r1 - 0.5) * 2.0 * (1.6 + z * uTanH * 1.15);
    float age = fract (r2 + uFlowAcc.y * fac);       // 0: on the floor .. 1: risen the box's height
    vec2 wob2 = vec2 (sin (uTime * 0.9 + r4 * 40.0), cos (uTime * 0.7 + r1 * 30.0)) * (0.2 + 0.4 * age);
    vec2 pr = uCamRel.xz + uRightLevel * (x + wob2.x) + uFwdLevel * (z + wob2.y);
    float h = age * uBox.y * (0.7 + 0.3 * age);
    P = vec3 (pr.x, floorH (pr) + 0.12 + h, pr.y);
    rel = P - uCamRel;
    size *= (1.0 + 1.2 * age);
    thin = 1.0 - 0.6 * age;
  } else {                                           // the bubbles
    float r6 = hs (pi, 6u), r7 = hs (pi, 7u);
    vec3 local;
    if (r6 < 0.45) {                                 // a plume from a thruster below the glass, left or right, speeding up as it rises
      float sd = r7 < 0.5 ? -1.0 : 1.0;
      float age = fract (r3 + uFlowAcc.y * fac);
      float rise = uBox.y * (0.45 * age + 0.55 * age * age);
      local.x = sd * 1.4 + (r1 - 0.5) * (0.3 + 2.2 * age) + 0.07 * sin (age * 25.0 + r2 * 6.2831853);
      local.y = -1.35 + rise;
      local.z = 0.45 + r2 * 1.6 - age * (0.5 + 0.06 * uSpeed) + 0.05 * cos (age * 19.0 + r1 * 6.2831853);
      if (r7 > 0.4 && r7 < 0.5 || r7 > 0.9) size *= 3.0;    // a few big ones, close to the glass
      thin = 1.0 - 0.3 * age;
    } else {                                         // the field: everywhere in the view's wedge, rising through it
      float age = fract (r3 + uFlowAcc.y * fac * 0.5);
      float z = 2.0 + r2 * (uBox.z - 2.0);
      float span = 1.6 + z * 0.56;
      local.x = (r1 - 0.5) * 2.0 * (1.2 + z * uTanH * 1.1) + 0.08 * sin (age * 20.0 + r4 * 6.2831853);
      local.y = -0.55 * span - 0.6 + (age * (0.6 + 0.4 * age)) * span * 1.25;
      local.z = z;
    }
    rel = uCamR * local.x + uCamU * local.y + uCamF * local.z;
    P = uCamRel + rel;
  }
  float dist = length (rel);
  vec3 wp;
  if (uKind > 0.5 && uKind < 1.5) {                  // the silt is stretched along the way it streams
    vec3 flow = normalize (-vec3 (uFwdLevel.x, 0.0, uFwdLevel.y) * (uSpeed + 0.3) + vec3 (0.0, 0.5, 0.0));
    vec3 side = normalize (cross (flow, rel / dist));
    float len = size + uSpeed * 0.05;
    wp = P + flow * (corner.x - 0.5) * len + side * (corner.y - 0.5) * size;
  } else wp = P + (uCamR * (corner.x - 0.5) + uCamU * (corner.y - 0.5)) * size;
  gl_Position = uViewProj * vec4 (wp, 1.0);
  vUv = corner;
  float f = fogAmount (dist);
  vF = f;
  float lit = uKind < 0.5 ? (0.35 + 1.6 * shafts (P)) : 1.0;
  float near = smoothstep (0.3, uKind > 1.5 ? 0.6 : 1.2, dist);
  vA = (1.0 - f) * near * thin * (0.5 + 0.5 * r5 / max (uAmount, 0.01));
  if (uKind < 0.5) { vCol = mix (vec3 (0.75, 0.95, 0.95), vec3 (1.0, 0.97, 0.85), r2) * lit * 0.9; vA *= 0.75; }
  else if (uKind < 1.5) { vCol = vec3 (0.64, 0.58, 0.50) * (0.6 + 0.4 * r2) + lamps (vec3 (0.0, 1.0, 0.0), rel) * 0.5; vA *= 0.26; }
  else { vCol = vec3 (0.85, 1.0, 1.0); vA *= 0.95; }
  if (float (pi) >= uThin.x) vA *= uThin.y;
}
