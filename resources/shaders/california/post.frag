
uniform sampler2D uColour, uDash;          // the scene (alpha: 1 where the sky shows, 0.25 a sign's face), the dashboard
uniform vec2 uSunUv; uniform float uSunOn, uAspect, uRayGain, uFlat;
uniform vec3 uRayCol;
uniform float uDashOn, uPanelGain;   // the cabin's dashboard over the view (straight alpha), a little darker in a shadow
// The censorship (the owner, 19.11): over each headstock a mosaic of big flat blocks, each the average of the scene
// under it, and a CENSORED label by it (screen rects, uv; x < 0 none)
uniform vec4 uMos[2]; uniform vec4 uMosBlk[2]; uniform vec4 uLab[2]; uniform vec2 uLabA; uniform sampler2D uLabel;   // uMosBlk: the block (uv), the fade
uniform vec2 uMosZ[2]; uniform vec2 uLabZ, uZnf;   // the boxes' depth range, the words' depth (m along the view); near, far
uniform sampler2D uDepth;                          // the scene's depth (only while something is censored)
float viewZ (vec2 q) { float d = texture (uDepth, q).r * 2.0 - 1.0; return 2.0 * uZnf.x * uZnf.y / (uZnf.y + uZnf.x - d * (uZnf.y - uZnf.x)); }
in vec2 vNdc;
out vec4 fragColor;
// the highlights rolled off by their brightest channel (the hue kept: a backlit poppy stays orange), the very bright
// (the sun) going to white
vec3 knee (vec3 c) {
  float m = max (max (c.r, c.g), c.b);
  if (m <= 0.8) return c;
  float k = 0.8 + 0.2 * (1.0 - exp (-(m - 0.8) / 0.2));
  return mix (c * (k / m), vec3 (k), smoothstep (1.0, 2.2, c.b + 0.5 * c.g) * 0.8);   // only the sun's own white core
}
void main() {
  vec2 uv = vNdc * 0.5 + 0.5;
  vec4 dash = vec4 (0.0);
  if (uDashOn > 0.5) {
    dash = texture (uDash, vec2 (uv.x, 1.0 - uv.y));
    if (dash.a > 0.996) { fragColor = vec4 (clamp (dash.rgb * uPanelGain, 0.0, 1.0), 1.0); return; }
  }
  vec4 sc = texture (uColour, uv);
  vec3 col = sc.rgb;
  for (int i = 0; i < 2; ++i) {
    vec4 R = uMos[i];
    if (R.x >= 0.0 && uv.x >= R.x && uv.x <= R.z && uv.y >= R.y && uv.y <= R.w) {
      // the mosaic: each block the average of the guitar's own pixels in it (their depth within the box's), none in
      // it: the block left clear (the sky, the hills); a pixel of anything nearer than the box (a pole, a wire, a
      // tree) stays over it
      vec2 Z = uMosZ[i];
      if (viewZ (uv) > Z.x - 0.4) {
        vec2 b = uMosBlk[i].xy, c0 = R.xy + floor ((uv - R.xy) / b) * b;      // the block's corner
        vec3 sum = vec3 (0.0); float n = 0.0;
        for (int j = 0; j < 3; ++j)
          for (int k = 0; k < 3; ++k) {
            vec2 q = c0 + b * vec2 ((float (j) + 0.5) / 3.0, (float (k) + 0.5) / 3.0);
            float z = viewZ (q);
            if (z > Z.x - 0.4 && z < Z.y + 0.4) { sum += texture (uColour, q).rgb; n += 1.0; }
          }
        if (n > 0.0) { col = mix (col, sum / n, uMosBlk[i].z); sc.a = 0.25; }   // no sun rays over it
      }
    }
  }
  if (uSunOn > 0.001 && (sc.a < 0.1 || sc.a > 0.5)) {                 // not over a sign's face
    vec2 dv = uSunUv - uv;
    float r = length (dv * vec2 (uAspect, 1.0));
    if (r < 0.55 && (r < 0.30 || sc.a > 0.5)) {
      float j = fract (52.9829189 * fract (dot (gl_FragCoord.xy, vec2 (0.06711056, 0.00583715))));
      float sum = 0.0, wsum = 0.0;
      for (int i = 0; i < 5; ++i) {
        float f = (float (i) + j) * 0.2;
        float w = (i % 2 == 1) ? 1.0 - uFlat : 1.0;     // the ladder's first rung: half the taps, faded
        sum += w * step (0.5, texture (uColour, uv + dv * f).a);    // the sky's alpha (1; outside the windshield's box too: the frame is not in the air)
        wsum += w;
      }
      float lit = sum / wsum;
      float fall = exp (-r * 3.4) * (1.0 - smoothstep (0.35, 0.55, r));
      col += uRayCol * lit * fall * uRayGain * uSunOn;
    }
  }
  col = knee (col);
  for (int i = 0; i < 2; ++i) {
    vec4 L = uLab[i];
    if (L.x >= 0.0 && uv.x >= L.x && uv.x <= L.z && uv.y >= L.y && uv.y <= L.w) {
      vec4 t = texture (uLabel, vec2 ((uv.x - L.x) / (L.z - L.x), (L.w - uv.y) / (L.w - L.y)));
      float wz = i == 0 ? uLabZ.x : uLabZ.y;
      if (viewZ (uv) > wz - 0.4) col = mix (col, t.rgb, t.a * (i == 0 ? uLabA.x : uLabA.y));   // anything nearer stays over the word
    }
  }
  float vig = 1.0 - 0.15 * dot (vNdc * vec2 (0.8, 1.0), vNdc * vec2 (0.8, 1.0));
  fragColor = vec4 (mix (col * vig, clamp (dash.rgb * uPanelGain, 0.0, 1.0), dash.a), 1.0);
}
