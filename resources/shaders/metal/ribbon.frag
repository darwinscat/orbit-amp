// From the prototype's metal-shaders.js (FS_RIBBON), its body as it is.

in float vAcross; in float vAlong; in vec4 vColour; in float vKind; in float vCov;
out vec4 fragColor;
void main() {
  float x = abs (vAcross);
  vec3 c;
  if (vKind < 0.5) {
    float core = exp (-x * x * 9.0), glow = exp (-x * x * 2.0) * (1.0 - x);
    c = (vec3 (0.9, 0.92, 1.0) * core * 2.6 + vColour.rgb * glow * 1.1) * vColour.a * min (vCov * 1.5, 1.0);
  } else {
    float beam = exp (-x * x * 3.0) * (1.0 - smoothstep (0.0, 1.0, vAlong)) * smoothstep (0.0, 0.03, vAlong);
    c = vColour.rgb * beam * vColour.a * min (vCov, 1.0);
  }
  fragColor = vec4 (c, 0.0);
}
