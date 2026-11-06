#include "walker/common.glsl"

in vec3 aPos; in vec3 aNrm; in vec2 aUv; in float aPart;
in vec4 aI0;                        // x, z (absolute), size, rotation
in vec4 aI1;                        // hue, phase, glow, lean
uniform mat4 uViewProj;
uniform vec4 uPrint[12];            // x, z (absolute), t0, radius
out vec3 vN; out vec3 vRel; out vec2 vUv; out float vPart; out vec3 vGlowCol; out float vGlow; out float vSize;
void main () {
  float size = aI0.z, rot = aI0.w;
  vec2 foot = aI0.xy - uCamXZ;
  float dist = length (foot);
  float glow = glowOf (aI1.z, aI1.y, dist);
  float crush = 0.0;
  if (size < 1.3) {
    for (int i = 0; i < 12; ++i) {
      float d = length (aI0.xy - uPrint[i].xy);
      float age = uTime - uPrint[i].z;
      if (age < 0.0) continue;
      crush = max (crush, smoothstep (uPrint[i].w, uPrint[i].w * 0.5, d) * smoothstep (0.0, 0.12, age));
    }
  }
  vec3 p = aPos * size;
  float swell = 1.0 + 0.05 * uBreath * (0.6 + 0.4 * sin (uTime * 0.7 + aI1.y));   // the caps breathe with the low note
  if (aPart > 0.5) { p.xz *= swell; p.y = (p.y - aI0.z * 0.6) * (1.0 + 0.03 * uBreath) + aI0.z * 0.6; }
  p.y *= 1.0 - 0.82 * crush; p.xz *= 1.0 + 0.4 * crush;
  p.x += aI1.w * p.y * p.y / size;
  float ca = cos (rot), sa = sin (rot);
  p.xz = vec2 (ca * p.x - sa * p.z, sa * p.x + ca * p.z);
  vec3 n = aNrm;
  n.xz = vec2 (ca * n.x - sa * n.z, sa * n.x + ca * n.z);
  float gh = groundAt0 (foot).r;
  vRel = vec3 (foot.x + p.x, gh + p.y, foot.y + p.z);
  vN = n; vUv = aUv; vPart = aPart; vSize = size;
  vGlowCol = palette (aI1.x); vGlow = glow * (1.0 - 0.7 * crush);
  gl_Position = uViewProj * vec4 (vRel, 1.0);
}
