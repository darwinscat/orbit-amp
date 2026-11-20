#define HTEX(uv) textureLod(uHeight, uv, 0.0)
#define RTEX(uv) textureLod(uRoad, uv, 0.0)
#define LTEX(uv) textureLod(uLand, uv, 0.0)
#include "california/common.glsl"
#include "california/light.glsl"

in vec3 aPos;     // the vertex in the butterfly's frame: x out (signed by the side), y up, z toward the head
in vec2 aUV;      // the wing's pattern point (out, along)
in float aPart;   // 0 the body, 1 a wing
in vec4 aI0;      // rel position, scale (metres for out = 1)
in vec4 aI1;      // yaw, flap (rad, the wings' angle up), pitch (nose up), visibility
uniform mat4 uViewProj; uniform float uPxAngle;
out vec2 vUV; out vec3 vRel; out vec3 vN; out float vPart; out float vDetail; flat out int vColour;
void main() {
  float ci = floor (aI1.w * 0.5), vis = aI1.w - 2.0 * ci;
  vColour = int (ci + 0.5);
  if (vis < 0.01) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vUV = vec2 (0.0); vRel = vec3 (1.0); vN = vec3 (0.0, 1.0, 0.0); vPart = 0.0; vDetail = 0.0; return; }
  vec3 p = aPos; vec3 nn = vec3 (0.0, 1.0, 0.0);
  if (aPart > 0.5) {
    float s = p.x < 0.0 ? -1.0 : 1.0, ax = abs (p.x), f = aI1.y;
    p = vec3 (s * ax * cos (f), ax * sin (f), p.z);
    nn = vec3 (-s * sin (f), cos (f), 0.0);
  }
  float cp = cos (aI1.z), sp = sin (aI1.z);
  p = vec3 (p.x, p.y * cp + p.z * sp, p.z * cp - p.y * sp);
  nn = vec3 (nn.x, nn.y * cp + nn.z * sp, nn.z * cp - nn.y * sp);
  float cy = cos (aI1.x), sy = sin (aI1.x);
  vec3 rgt = vec3 (cy, 0.0, sy), fwd = vec3 (sy, 0.0, -cy);
  vec3 wp = aI0.xyz + (rgt * p.x + vec3 (0.0, p.y, 0.0) + fwd * p.z) * aI0.w * vis;
  vN = rgt * nn.x + vec3 (0.0, nn.y, 0.0) + fwd * nn.z;
  vRel = wp - uCamRel; vUV = aUV; vPart = aPart;
  vDetail = clamp (aI0.w / (length (vRel) * uPxAngle) / 14.0 - 0.4, 0.0, 1.0);
  gl_Position = uViewProj * vec4 (wp, 1.0);
}
