in float vS; in float vA; in vec2 vN;
out vec4 o;
void main(){
  float a = vA * (1.0 - vS * vS);
  o = vec4(vec3(0.075, 0.062, 0.05) * a * (0.6 + 0.4 * vS), a * 0.06);   // a faint sheen, darker on one side
}
