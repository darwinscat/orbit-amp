in vec2 vQ; in vec2 vC; in float vR; in float vA;
uniform float uHorizonV;
uniform vec3 uSkyLo, uSkyHi, uSeaFar, uSeaNear, uMistC;   // display-space colours of the scene
out vec4 o;
void main(){
  float d2 = dot(vQ, vQ);
  if (d2 > 1.0 + 4.0 / max(vR, 1.0)) discard;
  float d = sqrt(d2);
  float edge = clamp((1.0 - d) * vR + 0.5, 0.0, 1.0);
  // a lens: what lies behind and around, shrunk and upside-down
  float sv = vC.y - vQ.y * 0.12 * (0.8 + 0.35 * d2);       // where this point of the lens looks: flipped
  float e = sv - uHorizonV;
  vec3 c = e > 0.0 ? mix(uSkyLo, uSkyHi, smoothstep(0.0, 0.45, e)) : mix(uSeaFar, uSeaNear, smoothstep(0.0, 0.5, -e));
  c = mix(c, uMistC, exp(-abs(e) / 0.02) * 0.6);
  c *= mix(1.1, 0.22, smoothstep(0.55, 1.0, d));                     // the dark rim
  vec2 hl = vQ - vec2(-0.3, 0.36);
  c += vec3(1.0, 0.93, 0.84) * (exp(-dot(hl, hl) * 38.0) * 0.85);     // the bright highlight
  vec2 cs = vQ - vec2(0.22, -0.5);
  c += vec3(1.0, 0.84, 0.6) * (exp(-dot(cs, cs) * 22.0) * 0.14);      // a soft caustic opposite
  float a = edge * vA;
  o = vec4(c * a, a);
}
