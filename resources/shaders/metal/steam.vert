// From the prototype's metal-shaders.js (VS_STEAM), its body as it is.
#include "metal/vtex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

uniform mat4 uViewProj; uniform vec3 uCamR, uCamU;
uniform ivec2 uVentBase; uniform int uVentW; uniform float uBreath;
out vec3 vCol; out vec2 vUv; out float vA; out vec2 vSeed;
void main() {
  int inst = gl_InstanceID;
  ivec2 cij = ivec2 (inst % uVentW, inst / uVentW) + uVentBase;
  int id = gl_VertexID; int pi = id / 6; int ci = id - pi * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  vec2 ctr; float ph;
  vec2 ctrRel = vec2 (0.0);
  bool ok = ventAt (cij, ctr, ph);
  if (ok) { ctrRel = ctr - uCamCell; ok = abs (roadDist (ctrRel).x) > 12.0; }
  if (! ok) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec3 (0.0); vUv = corner; vA = 0.0; vSeed = vec2 (0.0); return; }
  ivec2 c = (cij + VENT_M * 64) % VENT_M;
  uint vid = uint (c.x) * 1000u + uint (c.y), sp = uint (pi) + 100u;
  float r1 = hs (sp, vid + 1u), r2 = hs (sp, vid + 2u), r3 = hs (sp, vid + 3u), r4 = hs (sp, vid + 4u);
  float life = 5.0 + 3.0 * r1;
  float age = fract (uTime / life + r2);
  float born = uTime - age * life;
  float amp = (0.3 + 0.7 * smoothstep (0.04, 0.35, breath (born, ph))) * (0.45 + 0.55 * uBreath);
  float t = age * life;
  float rise = 1.1 * t, rad = 0.45 + 0.5 * t;
  vec2 drift = vec2 (0.35, 0.2) * t + vec2 (r3 - 0.5, r4 - 0.5) * 0.8 * t;
  float gy = floorH (ctrRel) + 0.4;
  vec3 P = vec3 (ctrRel.x + drift.x, gy + rise, ctrRel.y + drift.y);
  vec3 wp = P + uCamR * ((corner.x - 0.5) * 2.0 * rad) + uCamU * ((corner.y - 0.5) * 2.0 * rad);
  gl_Position = uViewProj * vec4 (wp, 1.0);
  float hAbove = rise + (corner.y - 0.5) * 2.0 * rad;
  float glow = exp (-max (hAbove, 0.0) / 1.4) * (0.25 + 0.75 * breath (uTime, ph));
  vCol = vec3 (0.55, 0.60, 0.62) * (uAurAmb * 4.0 + uStarAmb * 3.0) + vec3 (1.0, 0.45, 0.15) * glow * 1.1;
  vUv = corner; vSeed = vec2 (r3, r4) * 3.0;
  vA = 0.40 * amp * pow (1.0 - age, 1.5) * smoothstep (0.0, 0.06, age);
}
