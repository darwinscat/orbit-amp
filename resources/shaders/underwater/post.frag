#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

uniform sampler2D uColour, uDepth;
// Where the cabin's panel covers the view (UnderwaterWorld::PanelMask, R): nothing drawn here is seen.
uniform sampler2D uMask; uniform vec2 uMaskScale; uniform float uMaskOn;
uniform vec3 uCamFwd, uCamRight, uCamUp; uniform vec2 uTanHalf; uniform float uShear, uNear, uFar, uShaftGain;
in vec2 vNdc;
out vec4 fragColor;
void main() {
  if (uMaskOn > 0.5 && texelFetch (uMask, min (ivec2 (vec2 (vNdc.x * 0.5 + 0.5, 0.5 - vNdc.y * 0.5) * uMaskScale), textureSize (uMask, 0) - 1), 0).r > 0.5) discard;
  vec2 uv = vNdc * 0.5 + 0.5;
  vec3 col = texture (uColour, uv).rgb;
  float z = texture (uDepth, uv).r;
  vec3 dir = normalize (uCamFwd + uCamRight * (vNdc.x * uTanHalf.x) + uCamUp * ((vNdc.y - uShear) * uTanHalf.y));
  float sceneDist = 1.0e4;
  if (z < 0.99999) {
    float ndc = z * 2.0 - 1.0;
    float viewZ = 2.0 * uNear * uFar / ((uFar + uNear) - ndc * (uFar - uNear));
    sceneDist = viewZ / max (dot (dir, uCamFwd), 1.0e-3);
  }
  // The shafts: six samples along the ray, the pattern of columns parallel to the light; what lies behind the
  // scene's surface is not lit.
  float sum = 0.0;
  float ts[6]; ts[0] = 1.5; ts[1] = 4.0; ts[2] = 8.0; ts[3] = 14.0; ts[4] = 23.0; ts[5] = 36.0;
  float ws[6]; ws[0] = 0.10; ws[1] = 0.16; ws[2] = 0.20; ws[3] = 0.22; ws[4] = 0.18; ws[5] = 0.14;
  for (int k = 0; k < 6; ++k) {
    float t = ts[k];
    if (t < sceneDist) sum += shafts (uCamRel + dir * t) * ws[k] * exp (-t * uFogK * 0.5);
  }
  float upness = smoothstep (-0.35, 0.35, dir.y);
  float towards = 0.6 + 0.4 * max (dot (dir, uLight), 0.0);
  col += uShaftCol * sum * rays (dir) * uShaftGain * (0.35 + 0.65 * upness) * towards * (1.0 - 0.25 * uSilt);
  col = col / (1.0 + col * 0.10);
  float vig = 1.0 - 0.22 * dot (vNdc * vec2 (0.85, 1.0), vNdc * vec2 (0.85, 1.0));
  fragColor = vec4 (col * vig, 1.0);
}
