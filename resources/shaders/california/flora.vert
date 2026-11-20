#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define LTEX(uv) textureLod(uLand, uv, 0.0)
#include "california/common.glsl"
#include "california/light.glsl"

in vec3 aPos; in vec3 aNrm; in vec3 aCol; in vec2 aUV; in float aFlag; in float aSway;
uniform mat4 uViewProj;
uniform ivec2 uFloraBase; uniform int uFloraW, uFloraM; uniform float uFloraCell, uFloraKind, uFloraDensity; uniform vec2 uFloraSize;
uniform vec4 uFloraFar;        // shrinking from, gone by; and for the half a thinned view leaves out: from, by
uniform float uThin;
uniform float uGustPh;         // fract (the camera cell along the wind / the gusts' wavelength), from doubles
uniform float uViewBottom;     // the lowest NDC y the window shows (-1 the band; the cabin's glass ends higher)
out vec3 vN; out vec3 vRel; out vec3 vCol; out vec2 vUV; out float vFlag; out float vSeed; out float vSun;
void cull () { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vN = vec3 (0.0, 1.0, 0.0); vRel = vec3 (1.0); vCol = vec3 (0.0); vUV = vec2 (0.0); vFlag = 0.0; vSeed = 0.0; vSun = 0.0; }
void main() {
  int ii = gl_InstanceID;
  ivec2 cij = ivec2 (ii % uFloraW, ii / uFloraW) + uFloraBase;
  ivec2 c = (cij + uFloraM * 8) % uFloraM;
  vec2 cen = (vec2 (cij) + vec2 (hcell (c, 41u), hcell (c, 42u))) * uFloraCell;
  vec2 pr = cen - uCamCell;
  float far = length (pr - uCamFrac);
  bool odd = hcell (c, 47u) < 0.5;
  float goneAt = odd ? mix (uFloraFar.y, uFloraFar.w, uThin) : uFloraFar.y;
  float fromAt = odd ? mix (uFloraFar.x, uFloraFar.z, uThin) : uFloraFar.x;
  float r0 = hcell (c, 43u);
  if (far > goneAt || r0 > uFloraDensity) { cull (); return; }
  // the view: the root's place on the screen across (flowers are short; the ground is near the road's level)
  vec4 cp = uViewProj * vec4 (pr.x, uCamRel.y - 1.9, pr.y, 1.0);
  vec4 ct = uViewProj * vec4 (pr.x, uCamRel.y + 1.5, pr.y, 1.0);
  if (cp.w < 0.2 || abs (cp.x) > cp.w * 1.06 + 0.8 || ct.y < ct.w * (uViewBottom - 0.02)) { cull (); return; }
  vec4 land = landAt (pr);
  float P = land.b;
  float field = uFloraKind < 0.5 ? P : uFloraKind < 1.5 ? land.a : 0.9 - 0.4 * P;
  field *= smoothstep (4.9, 5.5, abs (roadDist (pr).x));          // none on the road (the land map is too coarse for its edge)
  if (r0 > field * uFloraDensity) { cull (); return; }
  float gh = floorH (pr);
  float seed = hcell (c, 44u);
  float H = mix (uFloraSize.x, uFloraSize.y, pow (hcell (c, 45u), 1.5)) * (1.0 - smoothstep (fromAt, goneAt, far));
  float yaw = 6.2831853 * hcell (c, 46u); float cy = cos (yaw), sy = sin (yaw);
  float phase = seed * 6.2831853;
  // The breeze: a lean with the wind, gusts rolling across the field in waves, a flutter.
  float ws = length (uWind); vec2 wd = uWind / max (ws, 1.0e-3);
  float gust = 0.5 + 0.5 * sin (6.2831853 * uGustPh + dot (pr, wd) * 0.21 - uTime * 1.9);
  vec2 lean = wd * (0.06 + 0.11 * gust) * ws / 2.5;
  vec2 flut = 0.035 * vec2 (sin (uTime * 2.7 + phase + aSway * 3.0), cos (uTime * 2.2 + phase * 1.3)) * (0.4 + gust);
  vec3 lp = aPos * H; vec3 ln = aNrm;
  lp.xz = vec2 (cy * lp.x - sy * lp.z, sy * lp.x + cy * lp.z);
  ln.xz = vec2 (cy * ln.x - sy * ln.z, sy * ln.x + cy * ln.z);
  vec2 disp = (lean + flut) * aSway * aSway * H * 1.6;
  if (aFlag > 1.5 && aFlag < 2.5) disp += 0.012 * H * vec2 (sin (uTime * 5.3 + phase * 3.0 + aUV.x * 4.0), cos (uTime * 4.7 + phase)) * aUV.y;
  lp.xz += disp;
  lp.y -= 0.35 * dot (disp, disp) / max (H, 0.05);
  vec3 wp = vec3 (pr.x + lp.x, gh + lp.y - 0.02, pr.y + lp.z);
  vN = ln; vRel = wp - uCamRel; vCol = aCol; vUV = aUV; vFlag = aFlag; vSeed = seed; vSun = shadowAt (pr).x * hillSun (pr).x;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
