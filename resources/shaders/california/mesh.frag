#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

uniform sampler2D uTex1, uTex2, uTex3;
uniform float uMeshSun, uMeshFade;
uniform vec4 uMeshLamp;              // x 1: a saucer's lamps (the rim's chase, the underglow's breath); y its clock; z a flash;
                                     // w 1: the metal painted (the big roadside things, lit by the sun)
in vec3 vRel; in vec3 vN; in vec2 vUV; in vec3 vCol; flat in int vKind; flat in int vTex; in vec3 vLoc;
out vec4 fragColor;
void main() {
  if (uMeshFade < 0.999 && fract (52.9829189 * fract (dot (gl_FragCoord.xy, vec2 (0.06711056, 0.00583715)))) > uMeshFade) discard;
  vec3 alb = vCol;
  if (vTex > 0) {
    vec4 t = vTex == 1 ? texture (uTex1, vUV) : vTex == 2 ? texture (uTex2, vUV) : texture (uTex3, vUV);
    if (t.a < 0.5) discard;
    alb = t.rgb;
  }
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  if (dot (n, V) > 0.0) n = -n;
  vec3 R = reflect (V, n);
  float fres = pow (1.0 - max (-dot (V, n), 0.0), 5.0);
  vec3 sky = skyColour (vec3 (R.x, max (R.y, 0.02), R.z));
  float glint = pow (max (dot (R, uSun), 0.0), 120.0) * uMeshSun;
  vec3 col;
  int kind = (uMeshLamp.w > 0.5 && vKind == 1) ? 0 : vKind;          // painted, not steel (glass stays glass)
  float saucer = step (0.5, uMeshLamp.x), flash = 1.0 + 2.2 * uMeshLamp.z;
  float breath = 0.6 + 0.4 * sin (uMeshLamp.y * 1.1);
  if (kind == 1) {                                                                                                          // metal
    col = lightGround (alb * 0.55, n, V, uMeshSun, 0.0) + sky * (0.25 + 0.6 * fres) * 0.7 + uSunCol * glint * 1.5;
    col += saucer * vec3 (0.35, 1.0, 0.75) * 0.22 * breath * flash * step (vLoc.z, -0.2) * (1.0 - smoothstep (1.4, 4.6, length (vLoc.xy)));
  }
  else if (kind == 2) col = alb * 0.08 + sky * (0.18 + 0.75 * fres) + uSunCol * glint * 2.0;                                          // glass
  else if (kind == 3) {                                                                                                     // a lamp
    if (saucer > 0.5) {
      // a saucer's: round the rim three bright groups chasing slowly; underneath the green glow, breathing
      float chase = pow (0.5 + 0.5 * cos (3.0 * atan (vLoc.y, vLoc.x) - uMeshLamp.y * 1.3), 4.0);
      col = alb * (length (vLoc.xy) > 3.5 ? 0.55 + 2.4 * chase : 0.8 + 1.7 * breath) * flash;
    } else col = alb * 1.5 + vec3 (1.0, 0.8, 0.5) * 0.25;
  }
  else                 col = lightGround (alb, n, V, uMeshSun, 0.15) + alb * 0.10;                                                     // painted
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 0.0);
}
