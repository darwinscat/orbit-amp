#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define LTEX(uv) textureLod(uLand, uv, 0.0)
#include "california/common.glsl"
#include "california/light.glsl"

in vec2 aGrid;
uniform vec2 uLvlOrigin; uniform float uCellSize;
uniform vec4 uMorph;            // the continuous centre (rel xz), where the morph starts and where it is whole (L-inf m)
uniform vec2 uCoarseOff;        // fract (uCamCell / uCoarse) * uCoarse
uniform float uCoarse;          // the next level's cell
uniform float uMarchK;          // the far levels march further
uniform float uFarCut;
uniform vec2 uFwdXZ;            // the way the camera looks, on the ground (unit)
uniform mat4 uViewProj;
out vec3 vPos; out vec3 vN; out float vD; out float vSunV;
out vec4 vF;                    // the poppies' field, the lupine's, how olive, the straw's tone
out vec4 vHaze;                 // what the haze and the far fade mix in, and how much
// beyond the road map (the far ground): a short march toward the sun
float sunMarch (vec2 pr, float h) {
  float occ = -1.0, t = 30.0;
  for (int i = 0; i < 3; ++i) { occ = max (occ, (floorH (pr + uSunXZ * t) - h) / t); t *= 3.2; }
  return 1.0 - smoothstep (uSunTan - 0.03, uSunTan + 0.02, occ);
}
void main() {
  vec2 pr = uLvlOrigin + aGrid * uCellSize;
  vec4 a, b, c; float d;
  float h0 = floorH (pr, a, b, c, d);
  float h = h0;
  // out of sight (more than 72 degrees off the way, past 30 m): no march, no normal, no morph — nothing shows them
  vec2 rxz = pr - uCamRel.xz; float rl = length (rxz);
  bool seen = rl < 30.0 || dot (rxz, uFwdXZ) > 0.31 * rl;
  float k = seen ? clamp ((max (abs (pr.x - uMorph.x), abs (pr.y - uMorph.y)) - uMorph.z) / max (uMorph.w - uMorph.z, 1.0), 0.0, 1.0) : 0.0;
  if (k > 0.0) {
    vec2 g = (pr + uCoarseOff) / uCoarse; vec2 gi = floor (g); vec2 f = g - gi;
    vec2 p00 = gi * uCoarse - uCoarseOff;
    float h00 = floorH (p00), h10 = floorH (p00 + vec2 (uCoarse, 0.0));
    float h01 = floorH (p00 + vec2 (0.0, uCoarse)), h11 = floorH (p00 + vec2 (uCoarse, uCoarse));
    h = mix (h0, mix (mix (h00, h10, f.x), mix (h01, h11, f.x), f.y), k);
  }
  float e = uCellSize;
  vN = vec3 (0.0, 1.0, 0.0); vSunV = 1.0;
  if (seen) {
    vec2 hs2 = hillSun (pr);
    if (hs2.y > 0.999) { vec2 nl = landAt (pr).rg; vN = vec3 (nl.x, sqrt (max (1.0 - dot (nl, nl), 0.0)), nl.y); }
    else { float hx = floorH (pr + vec2 (e, 0.0)), hz = floorH (pr + vec2 (0.0, e)); vN = normalize (vec3 (h0 - hx, e, h0 - hz)); }
    vSunV = hs2.y > 0.999 ? hs2.x : mix (sunMarch (pr, h0 + 0.4), hs2.x, hs2.y);
  }
  vPos = vec3 (pr.x, h, pr.y);
  vD = d;
  float ad = abs (d);
  vF = vec4 (poppyField (a, b, ad), lupineField (a, b, ad),
             clamp ((1.0 - smoothstep (0.30, 0.62, b.b)) * 0.55 + 0.35 * smoothstep (0.55, 0.8, a.g), 0.0, 0.8), c.r);
  vec3 rel = vPos - uCamRel; float dist = length (rel); vec3 V = rel / max (dist, 1.0e-3);
  vec3 hc = hazeColour (V);
  float hz2 = hazeAmount (dist) * 0.94, ff = smoothstep (uFarCut * 0.70, uFarCut, dist);
  float am = 1.0 - (1.0 - hz2) * (1.0 - ff);
  vHaze = vec4 ((hc * hz2 * (1.0 - ff) + ridgeFrom (V, hc, 0.0) * ff) / max (am, 1.0e-4), am);
  gl_Position = uViewProj * vec4 (vPos, 1.0);
}
