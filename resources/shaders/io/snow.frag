// From the prototype's io-shaders.js (FS_SNOW), its body as it is.

uniform float uExpo;
in vec4 vC; in vec2 vUv;
out vec4 fragColor;
void main() {
  float a = pow (1.0 - abs (vUv.y * 2.0 - 1.0), 1.4) * smoothstep (0.0, 0.25, vUv.x) * smoothstep (1.0, 0.75, vUv.x);
  vec3 c = vC.rgb * a * uExpo;
  fragColor = vec4 (c / (1.0 + max (max (c.r, c.g), c.b) * 0.25), vC.a * a);    // onto the canvas: its own gentle knee
}
