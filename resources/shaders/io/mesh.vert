// From the prototype's io-shaders.js (VS_MESH), its body as it is.
#include "io/vtex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

in vec3 aPos; in vec3 aNrm; in vec2 aUV; in vec3 aCol; in float aKind; in float aTex;
uniform mat4 uViewProj;
uniform vec3 uMeshAt; uniform mat3 uMeshRot;
uniform float uShadowPass;
out vec3 vPos; out vec3 vN; out vec3 vCol; flat out int vKind;
void main() {
  vec3 p = vec3 (aPos.x, aPos.z, -aPos.y), n = vec3 (aNrm.x, aNrm.z, -aNrm.y);
  vPos = uMeshAt + uMeshRot * p; vN = uMeshRot * n; vCol = aCol;
  vKind = int (aKind + 0.5);
  gl_Position = uShadowPass > 0.5 ? uViewProj * vec4 (vPos, 1.0) : project (uViewProj, vPos);
}
