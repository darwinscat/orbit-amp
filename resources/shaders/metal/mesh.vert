// From the prototype's metal-shaders.js (VS_MESH), its body as it is.
#include "metal/vtex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

in vec3 aPos; in vec3 aNrm; in vec2 aUV; in vec3 aCol; in float aKind; in float aTex;
uniform mat4 uViewProj;
uniform vec3 uMeshAt; uniform mat3 uMeshRot;   // the frame: rotation and scale (y up), the place (rel)
out vec3 vRel; out vec3 vN; out vec3 vCol; flat out int vKind; out vec3 vLoc;
void main() {
  vec3 p = vec3 (aPos.x, aPos.z, -aPos.y), n = vec3 (aNrm.x, aNrm.z, -aNrm.y);
  vec3 wp = uMeshAt + uMeshRot * p;
  vRel = wp - uCamRel; vN = uMeshRot * n; vCol = aCol; vLoc = aPos;
  vKind = int (aKind + 0.5);
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
