#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define LTEX(uv) textureLod(uLand, uv, 0.0)
#include "california/common.glsl"
#include "california/light.glsl"

uniform mat4 uViewProj;
uniform vec3 uBeamTop, uBeamFoot;    // rel: the saucer's underside, the ground under it
uniform vec2 uBeamR;                 // the radius at the top, at the foot
out vec3 vRel; out vec3 vN; out float vH;
void main() {
  int seg = gl_VertexID / 6, k = gl_VertexID % 6;
  int c = (k == 0 || k == 3) ? 0 : (k == 1) ? 1 : (k == 2 || k == 4) ? 2 : 3;      // two triangles: 0 1 2, 0 2 3
  float a = (float (seg) + ((c == 1 || c == 2) ? 1.0 : 0.0)) * (6.2831853 / 28.0);
  float h = c >= 2 ? 1.0 : 0.0;
  vec3 dir = vec3 (cos (a), 0.0, sin (a));
  vec3 p = mix (uBeamTop, uBeamFoot, h) + dir * mix (uBeamR.x, uBeamR.y, h);
  vRel = p - uCamRel; vN = dir; vH = h;
  gl_Position = uViewProj * vec4 (p, 1.0);
}
