#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

uniform sampler2D uAtlas;
uniform float uLightK, uMix, uFade;
in vec2 vQ; in vec2 vUV; in vec2 vUV1; in vec3 vRel;
out vec4 fragColor;
void main() {
  vec4 t = mix (texture (uAtlas, vUV), texture (uAtlas, vUV1), uMix);   // premultiplied: the two headings blended
  if (t.a < 0.003) discard;
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 c = t.rgb / t.a;
  float lamp = smoothstep (0.72, 0.95, max (c.r, max (c.g, c.b)));
  float f = fogAmount (dist * 0.75) * (1.0 - 0.5 * lamp);      // a little clearer than the floor: it reads far off
  fragColor = vec4 (mix (t.rgb * uLightK, waterColour (V) * t.a, f), t.a) * uFade;
}
