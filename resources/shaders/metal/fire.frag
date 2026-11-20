// From the prototype's metal-shaders.js (FS_FIRE), its body as it is.

in vec2 vUV; in vec4 vCol; in float vSoft;
out vec4 fragColor;
void main() {
  float r = length (vUV - 0.5) * 2.0;
  float a = 1.0 - smoothstep (0.0, 1.0, r); a *= a;
  fragColor = vCol * a;
}
