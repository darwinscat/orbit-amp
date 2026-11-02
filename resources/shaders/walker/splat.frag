
in vec2 vQ; in vec3 vCol;
uniform float uLmScale;
out vec4 fragColor;
void main () {
  float d2 = dot (vQ, vQ);
  float w = max (0.0, 1.0 - d2); w = w * w;
  fragColor = vec4 (vCol * w / uLmScale, 1.0);
}
