#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#include "underwater/common.glsl"

in vec3 aPos; in vec3 aNrm; in vec3 aCol; in vec2 aUV; in float aFlag;
uniform mat4 uViewProj;
uniform ivec2 uFloraBase; uniform int uFloraW, uFloraM; uniform float uFloraCell, uFloraKind, uFloraDensity; uniform vec2 uFloraSize;
uniform float uSpeed;
out vec3 vN; out vec3 vRel; out vec3 vPos; out vec3 vCol; out vec2 vUV; out float vEmis; out float vSeed;
float fieldFor (float kind, vec4 a, vec4 b, vec4 c) {
  if (kind < 0.5) return smoothstep (0.50, 0.60, b.g) * (1.0 - smoothstep (0.62, 0.72, b.g));           // fans: a band
  if (kind < 1.5) return smoothstep (0.60, 0.75, a.r) * smoothstep (0.45, 0.65, c.b);                   // bushes: the uplands
  if (kind < 2.5) return smoothstep (0.28, 0.40, b.g) * (1.0 - smoothstep (0.42, 0.50, b.g));           // anemones: another band
  if (kind < 3.5) return smoothstep (0.62, 0.72, b.r) * smoothstep (0.52, 0.75, c.g);                   // kelp: clumps
  return smoothstep (0.50, 0.65, c.r) * smoothstep (0.40, 0.60, b.b);                                   // polyps: patches
}
void main() {
  int ii = gl_InstanceID;
  ivec2 cij = ivec2 (ii % uFloraW, ii / uFloraW) + uFloraBase;
  ivec2 c = (cij + uFloraM * 8) % uFloraM;
  vec2 cen = (vec2 (cij) + vec2 (hcell (c, 41u), hcell (c, 42u))) * uFloraCell;
  vec2 pr = cen - uCamCell;
  float r0 = hcell (c, 43u);
  float far = length (pr - uCamFrac);
  // a cell its kind leaves empty however its field lies (the field is at most 1), or too far: known before the floor is read
  if (r0 > uFloraDensity || far > 70.0) {
    gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vN = vec3 (0, 1, 0); vRel = vec3 (0); vPos = vec3 (0); vCol = vec3 (0); vUV = vec2 (0); vEmis = 0.0; vSeed = 0.0; return;
  }
  vec4 la, lb, lc; float gh = floorH (pr, la, lb, lc);
  float field = fieldFor (uFloraKind, la, lb, lc);
  float mound = smoothstep (0.70, 0.92, lc.a);
  if (r0 > field * uFloraDensity || (uFloraKind > 0.5 && uFloraKind < 2.5 && mound > 0.3) || far > 70.0) {
    gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vN = vec3 (0, 1, 0); vRel = vec3 (0); vPos = vec3 (0); vCol = vec3 (0); vUV = vec2 (0); vEmis = 0.0; vSeed = 0.0; return;
  }
  float seed = hcell (c, 44u);
  float H = mix (uFloraSize.x, uFloraSize.y, pow (hcell (c, 45u), 1.5));
  float yaw = 6.2831853 * hcell (c, 46u); float cy = cos (yaw), sy = sin (yaw);
  float phase = seed * 6.2831853;
  float emis = step (9.5, aFlag); float motion = aFlag - 10.0 * emis;
  float v = aUV.y;
  vec3 lp = aPos; vec3 ln = aNrm;
  // The tentacles close when the rover is near, or pushes a bow wave at speed.
  if (motion > 1.5) {
    vec3 here = vec3 (pr.x, gh, pr.y) - uCamRel;
    float d = length (here);
    float curl = max (smoothstep (7.0, 2.5, d), smoothstep (14.0, 5.0, d) * smoothstep (3.0, 9.0, uSpeed));
    float k = curl * v;
    lp.xz *= 1.0 - 0.72 * k * v;
    lp.y = lp.y * (1.0 - 0.45 * k) + 0.05 * k;
    lp.xz += 0.05 * vec2 (sin (uTime * 2.6 + phase + v * 7.0 + aUV.x * 9.0), cos (uTime * 2.1 + phase * 1.3 + v * 6.0)) * v * (1.0 - curl);
  }
  // The sway: the current from +x, a heave and a flutter, by v^2; the kelp waves along its length.
  if (motion > 0.5) {
    vec2 curl2 = vec2 (0.16 + 0.08 * sin (uTime * 0.7 + phase), 0.06 * cos (uTime * 0.9 + phase * 1.3));
    vec2 lat = curl2 * v * v + 0.03 * vec2 (sin (uTime * 2.3 + phase + v * 5.0), cos (uTime * 1.9 + phase)) * v;
    if (uFloraKind > 2.5 && uFloraKind < 3.5) lat += 0.07 * vec2 (sin (uTime * 1.3 + phase + v * 6.0), cos (uTime * 1.1 + phase + v * 5.0)) * v;
    lp.xz += lat;                                   // in the mesh's frame, then turned by the yaw below
  }
  lp *= H;
  lp.xz = vec2 (cy * lp.x - sy * lp.z, sy * lp.x + cy * lp.z);
  ln.xz = vec2 (cy * ln.x - sy * ln.z, sy * ln.x + cy * ln.z);
  vec3 wp = vec3 (pr.x + lp.x, gh + lp.y - 0.03, pr.y + lp.z);
  vN = ln; vRel = wp - uCamRel; vPos = wp; vCol = aCol; vUV = aUV; vEmis = emis; vSeed = seed;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
