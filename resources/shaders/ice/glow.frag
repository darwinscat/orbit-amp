uniform vec3 uLamp;   // the lamps' light by the guitar: never drawn in silence
in vec2 vUv; in float vFade;
out vec4 fragColor;
void main() {
  float r2 = dot (vUv, vUv);
  float g = exp (-r2 * 5.0) * (1.0 - smoothstep (0.7, 1.0, r2));   // soft, and to nothing at the quad's edge
  fragColor = vec4 (uLamp * (0.55 * g * vFade), 1.0);
}
