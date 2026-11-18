#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#include "underwater/common.glsl"
#include "underwater/light.glsl"
#include "underwater/jelly.glsl"

in vec3 aTSI;    // x: t along the tendril 0..1; y: the ribbon's side 0/1; z: the tendril's index
uniform vec3 uCamR, uCamU;
out vec3 vRel; out vec3 vCol; out vec3 vGlow; out float vT; out float vSide; out float vPhase; out float vAlpha;
const float TENDRILS = 10.0;
vec3 tendrilPoint (float t, float j, float c, float phase, float len) {
  float a = (j + 0.5) / TENDRILS * 6.2831853;
  float rootR = 0.93 * (1.0 - 0.32 * c);
  vec2 dir = vec2 (cos (a), sin (a));
  vec3 p = vec3 (rootR * dir.x, -0.02, rootR * dir.y);
  p.y -= t * len;
  float w1 = sin (uTime * 1.1 + t * 5.0 + phase + j * 1.3);
  float w2 = cos (uTime * 0.8 + t * 4.0 + phase + j * 0.9 + 1.3);         // the phase enters with weight 1: periodic over its 2 pi wrap
  p.xz += vec2 (w1, w2) * 0.11 * len * t;
  p.xz -= dir * c * 0.28 * t * (1.0 - 0.5 * t);
  p.xz += dir * 0.10 * t * t * len;
  return p;
}
void main() {
  float c = contraction (aPhase);
  float t = aTSI.x, side = aTSI.y, j = aTSI.z;
  float len = aTend * (1.6 + 0.9 * fract (j * 0.618 + aTend * 7.0));   // per tendril, constant: never from the phase, which wraps
  mat3 F = bellFrame (aAxis);
  float t2 = min (t + 0.08, 1.0);
  vec3 p0 = aPos + F * (tendrilPoint (t, j, c, aPhase, len) * aScale);
  vec3 p1 = aPos + F * (tendrilPoint (t2, j, c, aPhase, len) * aScale);
  vec3 rel0 = p0 - uCamRel;
  vec3 along = normalize (p1 - p0);
  vec3 toEye = normalize (rel0);
  vec3 perp = normalize (cross (along, toEye));
  float width = aScale * (0.07 * (1.0 - 0.8 * t) + 0.01);
  if (j < 2.0) width *= 2.2;                                   // two thick oral arms
  vec3 wp = p0 + perp * (side - 0.5) * width;
  vRel = wp - uCamRel; vCol = aCol; vGlow = aGlow; vT = t; vSide = side; vPhase = aPhase + j * 2.1; vAlpha = aAlpha;
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
