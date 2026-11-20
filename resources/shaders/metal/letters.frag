// From the prototype's metal-shaders.js (FS_LETTERS), its body as it is.
#include "metal/ftex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

uniform sampler2D uLetters, uNoise;
uniform vec2 uLettersSize;                                  // the atlas, px
in vec2 vQ; in vec4 vRect; in float vSeed; in vec3 vRel; in vec4 vColRect; in float vHH;
out vec4 fragColor;
float gLod;
// the letters' coverage at p (0..1 over the cell), faded at the cell's edge and read no blurrier than mip 2 (so no
// neighbouring cell bleeds in)
float mask (vec2 p) {
  vec2 e = min (p, 1.0 - p);
  if (min (e.x, e.y) <= 0.0) return 0.0;
  float edge = smoothstep (0.0, 0.015, min (e.x, e.y));
  return textureLod (uLetters, vec2 (mix (vRect.x, vRect.z, p.x), mix (vRect.w, vRect.y, p.y)), gLod).a * edge;
}
void main() {
  vec2 p = vQ * 0.5 + 0.5;                                  // the letters 0..1, the flames' room above
  vec2 uvpx = vec2 (mix (vRect.x, vRect.z, p.x), mix (vRect.w, vRect.y, p.y)) * uLettersSize;
  gLod = clamp (log2 (max (length (dFdx (uvpx)), length (dFdy (uvpx)))), 0.0, 2.0);
  float t = uTime;
  float n1 = textureLod (uNoise, vec2 (p.x * 2.3 + vSeed, p.y * 0.8 - t * 0.55), 0.0).g;
  float n2 = textureLod (uNoise, vec2 (p.x * 5.1 - vSeed, p.y * 1.6 - t * 1.15), 0.0).b;
  float letter = mask (p + vec2 ((n2 - 0.5) * 0.004, 0.0));
  float fl = 0.0;
  float stepY = 0.45 / vHH * 0.5;                            // the flames' steps: 0.45 m each, whatever the text's size
  for (int i = 1; i <= 5; ++i) {
    float fi = float (i);
    vec2 o = vec2 ((n1 - 0.5) * 0.05 * fi + (n2 - 0.5) * 0.025 * fi, -fi * stepY);
    fl = max (fl, mask (p + o) * (1.0 - fi / 6.2));
  }
  float tongue = smoothstep (0.30, 0.78, n1 * 0.65 + n2 * 0.6 + 0.12 - max (p.y - 1.0, 0.0) * vHH * 0.18);
  fl *= tongue * (1.0 - letter);
  float flick = sin (t * 13.0 + vSeed * 40.0 + p.x * 21.0) * sin (t * 7.3 + p.x * 9.0);
  float glow = hitGlow ();
  vec3 core = mix (vec3 (1.0, 0.42, 0.06), vec3 (1.0, 0.9, 0.6), smoothstep (0.45, 1.0, letter * (0.7 + 0.35 * n2)));
  vec3 col = core * letter * (1.5 + 0.3 * flick + 1.6 * glow + 0.5 * uStorm);
  if (vColRect.x >= 0.0 && letter > 0.0) {
    // its own colours (the logo's chrome): shown as they are, lit up by the fire round it and by a hit
    vec4 cc = textureLod (uLetters, vec2 (mix (vColRect.x, vColRect.z, p.x), mix (vColRect.w, vColRect.y, p.y)), gLod);
    col = mix (col, cc.rgb * (0.95 + 0.15 * flick + 1.2 * glow) + vec3 (1.0, 0.4, 0.1) * 0.12 * (1.0 - p.y), smoothstep (0.2, 0.8, cc.a));
  }
  col += mix (vec3 (1.0, 0.55, 0.12), vec3 (0.75, 0.10, 0.02), smoothstep (0.0, 0.7, 1.0 - fl)) * fl * (1.2 + 0.25 * flick + 1.8 * glow + 0.5 * uStorm);
  col *= 1.0 - smoothstep (1000.0, 1500.0, length (vRel));
  if (vColRect.x < 0.0) col *= 1.0 - vColRect.z;                          // a fade (the CENSORED words come in with their guitar)
  fragColor = vec4 (col, 0.0);
}
