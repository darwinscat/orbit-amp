#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define LTEX(uv) textureLod(uLand, uv, 0.0)
#include "california/common.glsl"
#include "california/light.glsl"

in vec3 aPos; in vec3 aNrm; in float aPart;
in vec4 aI0;     // the foot (rel xyz), the size (oak: crown radius m; pole: height m)
in vec4 aI1;     // yaw, the height's stretch (oak) or the lean (pole), tint, the sun's visibility (the hills)
uniform mat4 uViewProj;
uniform float uKind;              // 0 an oak, 1 a pole
out vec3 vN; out vec3 vRel; out vec3 vLocal; out float vPart; out float vTint; out float vSun; out float vY;
void main() {
  float ca = cos (aI1.x), sa = sin (aI1.x);
  vec3 lp; vec3 ln = aNrm;
  if (uKind < 0.5) {
    lp = aPos * vec3 (aI0.w, aI0.w * aI1.y, aI0.w);
    ln = normalize (aNrm / vec3 (1.0, aI1.y, 1.0));
    float sway = aPart > 0.5 ? 0.05 * aI0.w * aPos.y * sin (uTime * 0.9 + aI1.z * 20.0 + aPos.x) : 0.0;   // the crown breathes
    lp.xz += uWind * sway * 0.25;
  } else if (uKind < 1.5) {
    lp = aPos * vec3 (1.0, aI0.w / 9.0, 1.0);
    lp.x += aI1.y * lp.y;                                  // old poles lean
  } else {
    lp = aPos;                                             // the windpump, in metres; its fan turned about the hub
    if (uKind > 2.5) {
      float c = cos (aI1.y), s2 = sin (aI1.y);
      lp.xy = vec2 (c * lp.x - s2 * lp.y, s2 * lp.x + c * lp.y);
      ln.xy = vec2 (c * ln.x - s2 * ln.y, s2 * ln.x + c * ln.y);
      lp += vec3 (0.0, 10.4, -0.62);
    }
  }
  lp.xz = vec2 (ca * lp.x - sa * lp.z, sa * lp.x + ca * lp.z);
  ln.xz = vec2 (ca * ln.x - sa * ln.z, sa * ln.x + ca * ln.z);
  vec3 wp = aI0.xyz + lp;
  vN = ln; vRel = wp - uCamRel; vLocal = aPos * 3.0 + aI1.z * 17.0; vPart = aPart; vTint = aI1.z; vSun = aI1.w;
  vY = aPos.y;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
