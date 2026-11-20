#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define LTEX(uv) textureLod(uLand, uv, 0.0)
#include "california/common.glsl"
#include "california/light.glsl"

in vec3 aPos; in vec3 aNrm; in vec2 aUV; in vec3 aCol; in float aKind; in float aTex;
uniform mat4 uViewProj;
uniform vec3 uMeshAt; uniform float uMeshYaw;
uniform mat3 uMeshTilt;              // after the turn: a lean (the saucers'), else none
out vec3 vRel; out vec3 vN; out vec2 vUV; out vec3 vCol; flat out int vKind; flat out int vTex; out vec3 vLoc;
void main() {
  vec3 p = vec3 (aPos.x, aPos.z, -aPos.y), n = vec3 (aNrm.x, aNrm.z, -aNrm.y);     // z up -> y up
  float c = cos (uMeshYaw), s = sin (uMeshYaw);
  p.xz = vec2 (c * p.x - s * p.z, s * p.x + c * p.z);
  n.xz = vec2 (c * n.x - s * n.z, s * n.x + c * n.z);
  p = uMeshTilt * p; n = uMeshTilt * n;
  vec3 wp = uMeshAt + p;
  vRel = wp - uCamRel; vN = n; vUV = vec2 (aUV.x, 1.0 - aUV.y); vCol = aCol; vLoc = aPos;
  vKind = int (aKind + 0.5); vTex = int (aTex + 0.5);
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
