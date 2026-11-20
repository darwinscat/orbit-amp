// The cabin over the view at the ladder's foot (MetalGl.cpp, a rung of 0.5): the view with its post pass done at the
// scene's pixels, scaled up here under the two plates, laid as the post pass lays them at the top rungs (post.frag).
uniform sampler2D uView, uDash, uDashFlash;
uniform float uDashMix;
uniform vec4 uSceneBox;
in vec2 vNdc;
out vec4 fragColor;
void main() {
  vec2 uv = vNdc * 0.5 + 0.5;
  vec2 duv = vec2 (uv.x, 1.0 - uv.y);
  vec4 dash;
  if (uDashMix < 0.002) dash = texture (uDash, duv);
  else if (uDashMix > 0.998) dash = texture (uDashFlash, duv);
  else dash = mix (texture (uDash, duv), texture (uDashFlash, duv), uDashMix);
  if (dash.a > 0.996) { fragColor = vec4 (dash.rgb, 1.0); return; }
  vec3 col = texture (uView, (uv - uSceneBox.xy) / uSceneBox.zw).rgb;
  fragColor = vec4 (col * (1.0 - dash.a) + dash.rgb, 1.0);
}
