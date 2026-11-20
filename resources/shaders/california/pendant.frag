
uniform sampler2D uPend;
uniform float uPanelGain, uGlint;
in vec2 vUV;
out vec4 fragColor;
void main() {
  vec4 d = texture (uPend, vUV);
  if (d.a < 0.004) discard;
  vec3 c = d.rgb * uPanelGain * (1.0 + uGlint * smoothstep (0.55, 0.95, dot (d.rgb, vec3 (0.33))));
  fragColor = vec4 (clamp (c, 0.0, 1.0) * d.a, d.a);
}
