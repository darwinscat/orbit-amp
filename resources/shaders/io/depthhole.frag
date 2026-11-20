// From the prototype's io-shaders.js (FS_DEPTH_HOLE), its body as it is.

uniform vec4 uHole; uniform float uHoleOn;
in vec3 vPos;
void main() { if (uHoleOn > 0.5 && vPos.x > uHole.x && vPos.z > uHole.y && vPos.x < uHole.z && vPos.z < uHole.w) discard; }
