in float vA; in vec2 vUv;
out vec4 fragColor;
void main() {
  float a = vA * pow (1.0 - abs (vUv.y * 2.0 - 1.0), 1.5) * smoothstep (0.0, 0.2, vUv.x) * smoothstep (1.0, 0.8, vUv.x);
  fragColor = vec4 (vec3 (0.92, 0.76, 0.56), a);
}
