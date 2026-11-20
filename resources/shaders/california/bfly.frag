#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"
#include "california/monarch.glsl"

in vec2 vUV; in vec3 vRel; in vec3 vN; in float vPart; in float vDetail; flat in int vColour;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  bool back = dot (n, V) > 0.0;
  if (back) n = -n;
  vec3 alb;
  float transl;
  if (vPart > 0.5) {
    vec4 m = monarch (vUV, back ? 1.0 : 0.0, vDetail, 0.001, wingColour (vColour));
    if (m.a < 0.5) discard;
    alb = m.rgb; transl = 1.4;
  } else { alb = vec3 (0.06, 0.05, 0.05); transl = 0.0; }
  vec2 pq = vRel.xz + uCamRel.xz;
  vec3 col = lightGround (alb, n, V, shadowAt (pq).x * hillSun (pq).x, transl) + alb * 0.12;
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 0.0);
}
