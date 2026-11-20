// From the prototype's io-shaders.js (FS_PLUME), its body as it is.

in vec2 vUV; in vec4 vCol;
out vec4 fragColor;
void main() {
  float r = length (vUV - 0.5) * 2.0;
  float a = 1.0 - smoothstep (0.0, 1.0, r); a *= a;
  fragColor = vCol * a;
}
