// From the prototype's io-shaders.js (FS_POST), its body as it is.

uniform sampler2D uColour, uDash, uDashDark;
uniform float uDashOn, uExpo, uDashMix;
uniform vec4 uDial;              // the dosimeter: its pivot (stage px), the scale's radius R (stage px; 0: none), canvas px a stage px
uniform vec2 uStage, uNeedle;    // the stage's size (px); the needle's way (stage px, y down)
uniform vec3 uNeedleCol;
in vec2 vNdc;
out vec4 fragColor;
vec3 knee (vec3 c) {
  float m = max (max (c.r, c.g), c.b);
  if (m <= 0.75) return c;
  float k = 0.75 + 0.25 * (1.0 - exp (-(m - 0.75) / 0.25));
  return mix (c * (k / m), vec3 (k), smoothstep (1.4, 4.0, m) * 0.75);
}
// the needle's distance (stage px, about the pivot): a blade tapering from 0.05 R at the pivot to 0.012 R at its tip (0.82 R:
// across the coloured bands into the ticks), a short tail (0.17 R), the round cap over the painted hub (0.085 R)
float needleSd (vec2 q, float R) {
  float a = dot (q, uNeedle), c = abs (q.x * uNeedle.y - q.y * uNeedle.x);
  float tip = 0.82 * R, tail = 0.17 * R;
  float hw = (a > 0.0) ? mix (0.050, 0.012, min (a / tip, 1.0)) * R : 0.042 * R;
  return min (max (c - hw, max (-tail - a, a - tip)), length (q) - 0.085 * R);
}
void main() {
  vec2 uv = vNdc * 0.5 + 0.5, duv = vec2 (uv.x, 1.0 - uv.y);
  vec4 dash = vec4 (0.0);
  if (uDashOn > 0.5) {
    if (uDashMix < 0.002) dash = texture (uDash, duv);
    else if (uDashMix > 0.998) dash = texture (uDashDark, duv);
    else dash = mix (texture (uDash, duv), texture (uDashDark, duv), uDashMix);
  }
  vec3 col;
  if (dash.a > 0.996) col = dash.rgb;                       // the plate opaque: the scene is not read
  else {
    col = texture (uColour, uv).rgb;
    col *= uExpo;
    col = knee (col);
    float vig = 1.0 - 0.16 * dot (vNdc * vec2 (0.8, 1.0), vNdc * vec2 (0.8, 1.0));
    col = col * vig * (1.0 - dash.a) + dash.rgb;
  }
  if (uDial.z > 0.0) {
    vec2 q = duv * uStage - uDial.xy;
    if (max (abs (q.x), abs (q.y)) < uDial.z) {
      // its soft shadow on the face (it stands a few mm proud of the dial), then the needle, a canvas pixel's edge
      float soft = max (1.2, 1.0 / uDial.w);
      col *= 1.0 - 0.3 * clamp (0.5 - needleSd (q - vec2 (0.5, 0.9), uDial.z) / soft, 0.0, 1.0);
      col = mix (col, uNeedleCol, clamp (0.5 - needleSd (q, uDial.z) * uDial.w, 0.0, 1.0));
    }
  }
  fragColor = vec4 (col, 1.0);
}
