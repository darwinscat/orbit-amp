#include <panel>
#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

uniform sampler2D uDash, uSea;
uniform float uPanelGain;
uniform vec2 uCauScale;
uniform vec2 uPanelOff1, uPanelOff2, uPanelOffW;   // continuous: from the way travelled (fract on the CPU)
// Where the panel is bare glass (UnderwaterWorld::PanelMask, G): nothing is drawn. And how flat it is drawn: 0 moved by
// the water, 1 the dashboard alone (k = 1) — a quality rung below the top; FLAT is that, with no sea and no caustics
// (drawn OVER only).
uniform sampler2D uMask; uniform vec2 uMaskScale; uniform float uFlat;
// OVER, below the top rung: the view comes at the scene's pixels (uPost) and is scaled up here, the panel laid over it
// in the same pass — every pixel written, none blended.
#ifdef OVER
uniform sampler2D uPost;
#define BARE { fragColor = vec4 (texture (uPost, vNdc * 0.5 + 0.5).rgb, 1.0); return; }
#else
#define BARE discard
#endif
in vec2 vNdc;
out vec4 fragColor;
void main() {
  vec2 uv = vec2 (vNdc.x * 0.5 + 0.5, 0.5 - vNdc.y * 0.5);
  vec2 m = texelFetch (uMask, min (ivec2 (uv * uMaskScale), textureSize (uMask, 0) - 1), 0).rg;
  if (m.g > 0.5) BARE;
  vec4 d = texture (uDash, uv);                         // straight alpha
  if (d.a < 0.004) BARE;
#ifdef OVER
  vec3 view = m.r > 0.5 ? vec3 (0.0) : texture (uPost, vNdc * 0.5 + 0.5).rgb;   // under a covered block: not seen
#endif
#ifdef FLAT
  fragColor = vec4 (view * (1.0 - d.a) + d.rgb * d.a, 1.0);
#else
  vec3 sea = texture (uSea, uv).rgb;
  // the floor's net, but through the sphere's curved acrylic: large, soft patches (coarse mips), sliding the same way
  vec2 pp = uv * uCauScale;
  vec2 w = (textureLod (uCaustic, uPanelOffW + pp / 19.0 + uTime * vec2 (0.007, 0.004), 3.5).bb - 0.5) * 0.45;
  float c1 = textureLod (uCaustic, uPanelOff1 + pp / 2.1 + uTime * vec2 (0.027, 0.017) + w, 4.0).r;
  float c2 = textureLod (uCaustic, uPanelOff2 + pp / 1.44 - uTime * vec2 (0.018, 0.030) - w * 0.8, 4.0).g;
  float k = clamp (1.0 + 0.28 * (c1 * c2 * uCauGain - 1.0), 0.9, 1.18) * uPanelGain;   // soft and quiet (Oleh, 16.11)
  k = mix (k, 1.0, uFlat);
#ifdef OVER
  fragColor = vec4 (view * (1.0 - d.a) + clamp (d.rgb + (k - 1.0) * sea, 0.0, 1.0) * d.a, 1.0);
#else
  fragColor = vec4 (clamp (d.rgb + (k - 1.0) * sea, 0.0, 1.0) * d.a, d.a);
#endif
#endif
}
