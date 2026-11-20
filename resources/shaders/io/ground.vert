// From the prototype's io-shaders.js (VS_GROUND), its body as it is.
#include "io/vtex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

in vec2 aGrid;
uniform vec2 uLvlOrigin; uniform float uCellSize;
uniform vec4 uMorph;            // the continuous centre (rel xz), where the morph starts and where it is whole (L-inf m)
uniform vec2 uCoarseOff; uniform float uCoarse;
uniform vec2 uFwdXZ;
uniform mat4 uViewProj;
uniform float uShadowPass;
out vec3 vPos; out vec3 vN; out vec4 vA; out vec4 vB; out vec4 vC; out float vD;
void main() {
  vec2 pr = uLvlOrigin + aGrid * uCellSize;
  vec4 a, b, c, f; float d;
  float h0 = floorH (pr, a, b, c, f, d);
  float h = h0;
  vec2 rxz = pr - uCamRel.xz; float rl = length (rxz);
  bool seen = rl < 30.0 || dot (rxz, uFwdXZ) > 0.31 * rl;
  float k = seen ? clamp ((max (abs (pr.x - uMorph.x), abs (pr.y - uMorph.y)) - uMorph.z) / max (uMorph.w - uMorph.z, 1.0), 0.0, 1.0) : 0.0;
  if (k > 0.0) {
    vec2 g = (pr + uCoarseOff) / uCoarse; vec2 gi = floor (g); vec2 fr = g - gi;
    vec2 p00 = gi * uCoarse - uCoarseOff;
    float h00 = floorH (p00), h10 = floorH (p00 + vec2 (uCoarse, 0.0));
    float h01 = floorH (p00 + vec2 (0.0, uCoarse)), h11 = floorH (p00 + vec2 (uCoarse, uCoarse));
    h = mix (h0, mix (mix (h00, h10, fr.x), mix (h01, h11, fr.x), fr.y), k);
  }
  vPos = vec3 (pr.x, h, pr.y); vA = a; vB = b; vC = c; vD = d;
  if (uShadowPass > 0.5) { vN = vec3 (0.0, 1.0, 0.0); gl_Position = uViewProj * vec4 (vPos, 1.0); return; }
  float e = max (uCellSize * 0.5, 0.4);
  vN = vec3 (h0 - floorH (pr + vec2 (e, 0.0)), e, h0 - floorH (pr + vec2 (0.0, e)));
  gl_Position = project (uViewProj, vPos);
}
