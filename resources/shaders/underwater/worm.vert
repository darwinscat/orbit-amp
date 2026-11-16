#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#include "underwater/common.glsl"

in vec3 aPos;     // x, z: the ring's unit circle; y: the ring's height 0..1
uniform mat4 uViewProj;
uniform ivec2 uWormBase; uniform int uWormW, uWormM; uniform float uWormCell;
out vec3 vN; out vec3 vRel; out vec3 vPos; out float vT; out float vSeed; out vec3 vTipCol;
void main() {
  int ii = gl_InstanceID;
  ivec2 cij = ivec2 (ii % uWormW, ii / uWormW) + uWormBase;
  ivec2 c = (cij + uWormM * 8) % uWormM;
  vec2 cen = (vec2 (cij) + vec2 (hcell (c, 31u), hcell (c, 32u))) * uWormCell;
  vec2 pr = cen - uCamCell;
  vec4 a, b, cc; float gh = floorH (pr, a, b, cc);
  float field = smoothstep (0.58, 0.72, a.b) * smoothstep (0.42, 0.7, cc.g);
  float r0 = hcell (c, 33u);
  float slopeish = smoothstep (0.70, 0.92, cc.a);
  if (r0 > field * 0.35 || slopeish > 0.3) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vN = vec3 (0, 1, 0); vRel = vec3 (0); vPos = vec3 (0); vT = 0.0; vSeed = 0.0; vTipCol = vec3 (0); return; }
  float seed = hcell (c, 34u);
  float H = mix (0.4, 2.8, pow (hcell (c, 35u), 2.2));
  float R = H * mix (0.028, 0.05, hcell (c, 36u));
  float t = aPos.y;
  float phase = seed * 6.2831853;
  // The sway: the current from +x, a slow heave and a flutter, more toward the tip.
  vec2 curl = vec2 (0.22 + 0.10 * sin (uTime * 0.7 + phase), 0.08 * cos (uTime * 0.9 + phase * 1.3)) * H;
  vec2 lat = curl * t * t + 0.04 * H * vec2 (sin (uTime * 2.1 + phase + t * 4.0), cos (uTime * 1.7 + phase)) * t;
  float taper = 1.0 - 0.55 * t;
  float bulb = smoothstep (0.80, 0.90, t) * (1.0 - smoothstep (0.92, 1.0, t));
  float rr = R * (taper + 0.9 * bulb);
  if (t > 0.999) rr = 0.0;
  vec3 lp = vec3 (aPos.x * rr + lat.x, t * H, aPos.z * rr + lat.y);
  vec3 wp = vec3 (pr.x + lp.x, gh + lp.y - 0.05, pr.y + lp.z);
  vec3 ln = normalize (vec3 (aPos.x, 0.25 - 0.5 * bulb, aPos.z));
  vN = ln; vRel = wp - uCamRel; vPos = wp; vT = t; vSeed = seed;
  vTipCol = seed < 0.5 ? vec3 (1.0, 0.55, 0.18) : vec3 (0.3, 0.95, 1.0);
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
