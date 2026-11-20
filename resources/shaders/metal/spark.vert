// From the prototype's metal-shaders.js (VS_SPARK), its body as it is.
#include "metal/vtex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

uniform mat4 uViewProj; uniform vec3 uCamR, uCamU;
uniform ivec2 uVentBase; uniform int uVentW; uniform float uPxAng, uRate;
out vec3 vCol; out vec2 vUv; out float vA;
void main() {
  int inst = gl_InstanceID;
  ivec2 cij = ivec2 (inst % uVentW, inst / uVentW) + uVentBase;
  int id = gl_VertexID; int si = id / 6; int ci = id - si * 6;
  vec2 corner = vec2 ((ci == 1 || ci == 2 || ci == 4) ? 1.0 : 0.0, (ci == 2 || ci == 4 || ci == 5) ? 1.0 : 0.0);
  vec2 ctr; float ph;
  vec2 ctrRel = vec2 (0.0);
  bool ok = ventAt (cij, ctr, ph);
  if (ok) { ctrRel = ctr - uCamCell; ok = abs (roadDist (ctrRel).x) > 12.0; }
  if (! ok) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec3 (0.0); vUv = corner; vA = 0.0; return; }
  ivec2 c = (cij + VENT_M * 64) % VENT_M;
  uint vid = uint (c.x) * 1000u + uint (c.y), sp = uint (si);
  float r1 = hs (sp, vid + 1u), r2 = hs (sp, vid + 2u), r3 = hs (sp, vid + 3u), r4 = hs (sp, vid + 4u), r5 = hs (sp, vid + 5u), r6 = hs (sp, vid + 6u);
  float life = 1.5 + 0.9 * r4;
  float age = fract (uTime / life + r1);
  float born = uTime - age * life;
  float b = breath (born, ph);
  if (r2 > (0.12 + b) * uRate) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vCol = vec3 (0.0); vUv = corner; vA = 0.0; return; }
  float t = age * life;
  float gy = floorH (ctrRel) + 0.25;
  vec3 v0 = vec3 ((r3 - 0.5) * 2.2, 2.6 + 2.2 * r5, (r6 - 0.5) * 2.2);
  vec3 P = vec3 (ctrRel.x, gy, ctrRel.y) + v0 * t + vec3 (0.35 * t, -1.1 * t * t, 0.15 * t);
  float dist = length (P - uCamRel);
  float size = max (0.08, 2.6 * dist * uPxAng);
  vec3 wp = P + uCamR * ((corner.x - 0.5) * 2.0 * size) + uCamU * ((corner.y - 0.5) * 2.0 * size);
  gl_Position = uViewProj * vec4 (wp, 1.0);
  vUv = corner;
  vA = (1.0 - age) * smoothstep (0.0, 0.05, age) * (1.0 - smoothstep (60.0, 300.0, dist));
  vCol = mix (vec3 (1.0, 0.85, 0.5), vec3 (1.0, 0.25, 0.03), smoothstep (0.0, 0.6, age)) * (1.0 - 0.6 * smoothstep (0.6, 1.0, age));
}
