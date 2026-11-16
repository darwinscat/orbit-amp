#define HTEX(uv) texture(uHeight, uv)
#include "underwater/common.glsl"
#include "underwater/light.glsl"

in vec3 vN; in vec3 vRel; in vec3 vCol; in vec3 vGlow; in vec2 vUV; in float vPulse; in float vAlpha;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  float nv = abs (dot (n, V));
  float fres = pow (max (1.0 - nv, 0.0), 2.4);
  float light = 0.4 + 0.6 * max (dot (n, uLight), 0.0);
  float ribs = 0.5 + 0.5 * cos (vUV.x * 6.2831853 * 8.0);
  float rings = 0.5 + 0.5 * cos (vUV.y * 16.0 - uTime * 2.2);
  float lantern = pow (ribs, 14.0) * pow (rings, 9.0) * smoothstep (0.18, 0.4, vUV.y) * smoothstep (1.0, 0.86, vUV.y);
  float rimLine = smoothstep (0.90, 0.98, vUV.y) * (1.0 - smoothstep (0.98, 1.0, vUV.y));
  vec3 body = vCol * (0.35 + 0.6 * fres) * light + vCol * 0.25 * (1.0 - nv) + vec3 (0.6, 0.8, 0.8) * 0.12 * light;
  vec3 emis = vGlow * (lantern * 1.2 + rimLine * 0.7 * (0.5 + 0.5 * vPulse)) + vCol * 0.6 * pow (1.0 - vUV.y, 2.5) * (0.6 + 0.4 * vPulse);
  float a = (0.28 + 0.55 * fres + 0.10 * rimLine) * vAlpha;
  float f = fogAmount (dist);
  vec3 col = mix (body, waterColour (V), f) * a + emis * hitK (dist) * vAlpha * (1.0 - f);
  fragColor = vec4 (col, a);
}
