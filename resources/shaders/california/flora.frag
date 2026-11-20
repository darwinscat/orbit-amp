#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

uniform float uFloraKind;
in vec3 vN; in vec3 vRel; in vec3 vCol; in vec2 vUV; in float vFlag; in float vSeed; in float vSun;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  if (dot (n, V) > 0.0) n = -n;                       // thin things: lit from the side we see
  vec3 alb = vCol;
  float transl = 0.35;
  if (vFlag > 1.5 && vFlag < 2.5) {
    // A petal: deeper at its base, a sheen of fine veins, its rim catching the light.
    float base = smoothstep (0.0, 0.45, vUV.y);
    float veins = 0.94 + 0.06 * sin (vUV.x * 40.0);
    alb = mix (alb * vec3 (1.0, 0.72, 0.45), alb, base) * veins;
    transl = 1.3;
    n = normalize (mix (n, -V, 0.35));
  } else if (vFlag > 2.5) {
    transl = 0.2;
  } else if (vFlag > 0.5) {
    transl = 1.0;
  }
  vec3 col = lightGround (alb, n, V, vSun, transl);
  if (vFlag > 1.5 && vFlag < 2.5) {
    float through = 0.30 + 0.70 * pow (max (dot (V, uSun), 0.0), 2.0);
    col += alb * uSunCol * through * 0.40 * vSun + alb * uAmbUp * 0.45;
  }
  float hk = hitK (dist) - 1.0;
  if (hk > 0.0 && vFlag > 1.5 && vFlag < 2.5) col += alb * hk * 0.8 * exp (-dist / 30.0);
  if (uBeamPool.w > 0.001) col += alb * vec3 (0.45, 0.95, 0.65) * 0.9 * beamPool (uCamRel.xz + vRel.xz);   // under a beam
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 0.0);
}
