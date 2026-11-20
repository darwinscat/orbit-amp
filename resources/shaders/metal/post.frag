// From the prototype's metal-shaders.js (FS_POST), its body as it is but for the scene's reads: through uSceneBox, where the
// scene's frame sits in the whole view's (MetalGl.cpp: only the cabin's window is drawn; (0, 0, 1, 1) the whole view).

uniform sampler2D uColour, uDash, uDashFlash;
uniform float uDashOn, uLift, uDashMix;
uniform vec4 uSceneBox;
vec4 scene (vec2 uv) { return texture (uColour, (uv - uSceneBox.xy) / uSceneBox.zw); }
uniform vec4 uCens[4], uCensB[4];         // the censored headstocks: their box on the screen (uv), the blocks' size (uv) and on
in vec2 vNdc;
out vec4 fragColor;
vec3 knee (vec3 c) {
  float m = max (max (c.r, c.g), c.b);
  if (m <= 0.8) return c;
  float k = 0.8 + 0.2 * (1.0 - exp (-(m - 0.8) / 0.2));
  return mix (c * (k / m), vec3 (k), smoothstep (1.6, 3.5, m) * 0.7);
}
void main() {
  vec2 uv = vNdc * 0.5 + 0.5;
  vec4 dash = vec4 (0.0);
  if (uDashOn > 0.5) {
    // the plates, premultiplied: the night's, the flash's share crossfaded in (one fetch outside a flash)
    vec2 duv = vec2 (uv.x, 1.0 - uv.y);
    if (uDashMix < 0.002) dash = texture (uDash, duv);
    else if (uDashMix > 0.998) dash = texture (uDashFlash, duv);
    else dash = mix (texture (uDash, duv), texture (uDashFlash, duv), uDashMix);
    if (dash.a > 0.996) { fragColor = vec4 (dash.rgb, 1.0); return; }    // the plate opaque: the scene is not read
  }
  vec4 sc = scene (uv);
  vec3 col = sc.rgb;
  // THE CENSORSHIP MOSAIC (the owner's statement, honest, not hidden): inside a headstock's box the pixels become big flat
  // blocks, each the mean of five taps of what is under it
  // only the headstock's own pixels (marked 0.5 in the scene's alpha by the mesh: what stands nearer overwrote the mark,
  // the sky has its own); each block the mean of its taps that are the headstock's
  float own = abs (sc.a - 0.5) < 0.15 ? 1.0 : 0.0;
  for (int i = 0; i < 4; ++i) {
    vec4 r = uCens[i]; vec4 b = uCensB[i];
    if (own > 0.5 && b.z > 0.5 && uv.x > r.x && uv.x < r.z && uv.y > r.y && uv.y < r.w) {
      vec3 under = col;
      vec2 c0 = (floor ((uv - r.xy) / b.xy) + 0.5) * b.xy + r.xy;
      vec2 q = b.xy * 0.3;
      vec3 acc = vec3 (0.0); float wsum = 0.0;
      for (int t = 0; t < 5; ++t) {
        vec2 o = t == 0 ? vec2 (0.0) : t == 1 ? -q : t == 2 ? vec2 (q.x, -q.y) : t == 3 ? vec2 (-q.x, q.y) : q;
        vec4 tp = scene (c0 + o);
        float w = abs (tp.a - 0.5) < 0.15 ? 1.0 : 0.0;
        acc += tp.rgb * w; wsum += w;
      }
      col = wsum > 0.0 ? acc / wsum : col;
      // so the square reads as a mosaic even over the smooth sky: each block a little lighter or darker, a faint grid
      vec2 cell = floor ((uv - r.xy) / b.xy), f = fract ((uv - r.xy) / b.xy);
      float jit = fract (sin (dot (cell + float (i) * 17.0, vec2 (12.9898, 78.233))) * 43758.5453);
      vec2 px = b.xy / max (fwidth (uv), vec2 (1e-6));                     // a block's size in pixels
      float edge = step (min (f.x, f.y), 1.0 / max (min (px.x, px.y), 1.0));
      col = (col * (0.86 + 0.28 * jit) + vec3 (0.015)) * (1.0 - 0.28 * edge);
      col = mix (under, col, b.w);                                          // fading in as the headstock grows on the screen
    }
  }
  col = knee (col + uLift * vec3 (0.7, 0.72, 1.0));
  float vig = 1.0 - 0.18 * dot (vNdc * vec2 (0.8, 1.0), vNdc * vec2 (0.8, 1.0));
  col *= vig;
  fragColor = vec4 (col * (1.0 - dash.a) + dash.rgb, 1.0);              // the plate over it (premultiplied)
}
