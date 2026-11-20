// From the prototype's metal-shaders.js (VS_DRAGON), its body as it is.
#include "metal/vtex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

in vec3 aP0; in vec3 aN0; in vec3 aP1; in vec3 aN1; in vec3 aCol; in float aKind;
uniform mat4 uViewProj;
uniform vec3 uMeshAt; uniform mat3 uMeshRot;
uniform float uMix, uUnit;                      // between the two poses; metres per unit of the poses' int16
uniform vec2 uHead;                             // the head's yaw and pitch (perched gestures): the neck bends, the body stays
out vec3 vRel; out vec3 vN; out vec3 vCol; flat out int vKind; out vec3 vLoc;
void main() {
  vec3 q = mix (aP0, aP1, uMix) * uUnit, m = mix (aN0, aN1, uMix);
  if (uHead.x != 0.0 || uHead.y != 0.0) {
    // as the world's bendNeck: weighted from the neck's base (y -0.6) to the head (y -2.2), yaw about z then pitch about x
    float w = 1.0 - smoothstep (-2.2, -0.6, q.y);
    float a = uHead.x * w, b = uHead.y * w, ca = cos (a), sa = sin (a), cb = cos (b), sb = sin (b);
    vec3 d = q - vec3 (0.0, -0.6, -0.5);
    vec2 xy = vec2 (d.x * ca - d.y * sa, d.x * sa + d.y * ca);
    q = vec3 (xy.x, xy.y * cb + d.z * sb, d.z * cb - xy.y * sb) + vec3 (0.0, -0.6, -0.5);
    vec2 nxy = vec2 (m.x * ca - m.y * sa, m.x * sa + m.y * ca);
    m = vec3 (nxy.x, nxy.y * cb + m.z * sb, m.z * cb - nxy.y * sb);
  }
  vec3 p = vec3 (q.x, q.z, -q.y), n = vec3 (m.x, m.z, -m.y);
  vec3 wp = uMeshAt + uMeshRot * p;
  vRel = wp - uCamRel; vN = uMeshRot * n; vCol = aCol; vLoc = q;
  vKind = int (aKind + 0.5);
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
