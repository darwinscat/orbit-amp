#include "california/monarch.glsl"

in vec2 vQ; in float vOpen; in float vA; in float vLight; flat in int vColour;
out vec4 fragColor;
void main() {
  // the quad spans the open wings: x -1..1 (tip to tip), y -1..1 (the hindwings' tails .. the antennae)
  vec2 q = vQ;
  float ax = abs (q.x);
  vec4 w = vec4 (0.0);
  if (ax < vOpen) w = monarch (vec2 (ax / max (vOpen, 0.06), q.y * 1.05 + 0.02), 1.0, 1.0, 0.012 / max (vOpen, 0.3), wingColour (vColour));
  vec3 col = w.rgb * (0.55 + 0.75 * vLight) * (0.75 + 0.25 * vOpen);
  float a = w.a;
  // the body, the head, the antennae with their clubs
  float body = 1.0 - smoothstep (0.035, 0.05, length (vec2 (q.x, (q.y + 0.05) * 0.10)));
  float head = 1.0 - smoothstep (0.045, 0.06, length (q - vec2 (0.0, 0.47)));
  float ant = 0.0;
  for (int k = 0; k < 2; ++k) {
    float sd = k == 0 ? -1.0 : 1.0;
    vec2 p = q - vec2 (0.0, 0.48);
    vec2 dirA = normalize (vec2 (0.28 * sd, 1.0));
    float t = clamp (dot (p, dirA), 0.0, 0.42);
    ant = max (ant, 1.0 - smoothstep (0.007, 0.013, length (p - dirA * t)));
    ant = max (ant, 1.0 - smoothstep (0.016, 0.024, length (p - dirA * 0.42)));
  }
  float dots = step (0.6, fract (q.y * 30.0)) * (1.0 - smoothstep (0.012, 0.02, abs (q.x))) * step (q.y, 0.4) * step (-0.4, q.y);
  float dark = max (max (body, head), ant);
  col = mix (col, mix (vec3 (0.04, 0.03, 0.03), vec3 (0.85), dots * body * 0.6), dark);
  a = max (a, dark) * vA;
  fragColor = vec4 (col * a, a);
}
