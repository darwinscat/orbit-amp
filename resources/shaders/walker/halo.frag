
in vec2 vQ; in vec3 vCol; in float vDist;
uniform float uFogK;
out vec4 fragColor;
void main () {
  float d2 = dot (vQ, vQ);
  float w = max (0.0, 1.0 - d2); w = w * w * w;
  float f = exp (-vDist * uFogK * 0.8);
  fragColor = vec4 (vCol * w * f, 0.0);
}
