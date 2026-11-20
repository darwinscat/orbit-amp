#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

uniform sampler2D uAtlas;
uniform float uMix, uFade, uHazeDist, uLight, uRidge;
uniform vec2 uRimStep;              // a step in the atlas toward the sun on the screen
uniform vec3 uRimCol, uTint;
in vec2 vQ; in vec2 vUV; in vec2 vUV1; in vec3 vRel;
out vec4 fragColor;
void main() {
  vec4 t = mix (texture (uAtlas, vUV), texture (uAtlas, vUV1), uMix);
  if (t.a < 0.003) discard;
  vec3 V = normalize (vRel);
  float a2 = mix (texture (uAtlas, vUV + uRimStep).a, texture (uAtlas, vUV1 + uRimStep).a, uMix);
  float rim = clamp (t.a - a2, 0.0, 1.0);
  vec3 c = t.rgb * uTint * uLight + uRimCol * rim * t.a;
  vec3 hz = uRidge > 0.5 ? ridgeColour (V, 0.0) : hazeColour (V);
  float f = uRidge > 0.5 ? 0.5 : hazeAmount (uHazeDist) * 0.94;
  fragColor = vec4 (mix (c, hz * t.a, f), t.a) * uFade;
}
