
in float vA; in vec2 vUv; in vec3 vCol;
out vec4 fragColor;
void main () {
  float dy = vUv.y * 2.0 - 1.0;
  float a = vA * exp (-3.5 * dy * dy) * smoothstep (0.0, 0.3, vUv.x) * smoothstep (1.0, 0.7, vUv.x);
  fragColor = vec4 ((vCol * 0.85 + vec3 (0.12)) * a, 0.0);
}
