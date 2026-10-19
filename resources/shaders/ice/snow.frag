in float vA; in vec2 vUv;
out vec4 fragColor;
void main() {
  float a = vA * pow (1.0 - abs (vUv.y * 2.0 - 1.0), 1.4) * smoothstep (0.0, 0.25, vUv.x) * smoothstep (1.0, 0.75, vUv.x);
  fragColor = vec4 (vec3 (0.95, 0.97, 1.0), a);
}
