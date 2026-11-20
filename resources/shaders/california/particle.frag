#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

in vec2 vUv; in float vA; in vec3 vCol; in float vK;
out vec4 fragColor;
void main() {
  vec2 d = vUv * 2.0 - 1.0; float r = length (d);
  float a;
  if (vK < 0.5) a = smoothstep (1.0, 0.0, r) * smoothstep (1.0, 0.3, r);                 // a mote
  else if (vK < 1.5) {                                                                      // a dandelion seed: a star of fine hairs
    float ang = atan (d.y, d.x);
    float hairs = pow (abs (cos (ang * 6.0)), 18.0) * (1.0 - smoothstep (0.3, 1.0, r));
    a = max (hairs * 0.8, (1.0 - smoothstep (0.0, 0.22, r)) * 0.9) + (1.0 - smoothstep (0.4, 1.0, r)) * 0.15;
  } else if (vK < 2.5) a = 1.0 - smoothstep (0.55, 1.0, length (d * vec2 (1.0, 1.4)));    // a petal
  else {                                                                                    // a bird: a dark gull-wing V, beating
    float ph = vK - 3.0;
    float fy = 0.45 * sin (ph * 6.2831853);
    float x = abs (d.x);
    float yLine = fy * x + 0.25 * x * (1.0 - x) * (fy < 0.0 ? -1.0 : 1.0);
    float th = 0.11 * (1.0 - 0.8 * x) + 0.02;
    a = (1.0 - smoothstep (th * 0.6, th, abs (d.y - yLine))) * step (x, 0.98);
    a = max (a, 1.0 - smoothstep (0.08, 0.14, length (d * vec2 (1.0, 1.6))));
  }
  a = clamp (a, 0.0, 1.0) * vA;
  fragColor = vec4 (vCol * a, a);
}
