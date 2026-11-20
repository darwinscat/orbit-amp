// From the prototype's metal-shaders.js (FS_EMBER), its body as it is.

in float vA; in vec2 vUv;
out vec4 fragColor;
void main() {
  float a = vA * pow (1.0 - abs (vUv.y * 2.0 - 1.0), 1.5) * smoothstep (0.0, 0.2, vUv.x) * (1.0 - smoothstep (0.8, 1.0, vUv.x));
  fragColor = vec4 (vec3 (1.0, 0.42, 0.10) * a, 0.0);
}
