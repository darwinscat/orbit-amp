#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

in vec3 vRel; in vec3 vN; in vec2 vFace;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  vec3 R = reflect (V, n);
  float fres = 0.04 + 0.96 * pow (1.0 - max (-dot (V, n), 0.0), 5.0);
  vec3 sky = skyColour (vec3 (R.x, max (R.y, 0.0), R.z));
  vec3 col = vec3 (0.012, 0.012, 0.016) + sky * (0.10 + 0.55 * fres);
  col += vec3 (2.4, 1.8, 1.1) * pow (max (dot (R, uSun), 0.0), 900.0);
  float edge = 1.0 - smoothstep (0.0, 0.010, min (1.0 - abs (vFace.x), 1.0 - abs (vFace.y)));     // a thin warm rim
  float toward = max (dot (n, uSun), 0.0) * 0.6 + pow (max (dot (V, uSun), 0.0), 3.0) * 0.8;
  col += vec3 (1.0, 0.62, 0.30) * edge * toward * 0.9;
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 0.0);
}
