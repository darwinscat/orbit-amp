#include "ice/light.glsl"

uniform sampler2D uDetail; uniform float uJupCos;
in vec3 vN; in vec3 vRel; in vec3 vWorld; in float vSeed;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  vec3 R = reflect (V, n);
  // The environment a brushed surface sees: the black sky, Jupiter as a soft warm blob, the bright ice below, the mist band.
  float jb = smoothstep (uJupCos - 0.12, uJupCos + 0.02, dot (R, uJup));
  vec3 skyE = vec3 (0.01, 0.015, 0.03) + vec3 (0.78, 0.64, 0.50) * 0.75 * jb;
  vec3 iceE = vec3 (0.82, 0.88, 0.97) * (0.7 + 0.3 * max (-R.y, 0.0));
  vec3 env = mix (iceE, skyE, smoothstep (-0.04, 0.06, R.y));
  env = mix (env, uMist, 0.5 * exp (-abs (R.y) / 0.05));
  float cosT = clamp (dot (n, -V), 0.0, 1.0);    // to one as well: past it by an ulp, pow below would take a negative
  float fres = 0.56 + 0.44 * pow (1.0 - cosT, 5.0);
  vec3 H = normalize (uSun - V);
  float ndh = max (dot (n, H), 0.0);
  vec3 sunC = vec3 (1.0, 0.96, 0.90) * uSunInt;
  vec3 col = env * fres * 0.85
           + sunC * (pow (ndh, 110.0) * 1.5 + pow (ndh, 10.0) * 0.12) * step (0.0, dot (n, uSun))
           + vec3 (0.30, 0.44, 0.70) * 0.10 * (0.5 + 0.5 * n.y) + sunC * 0.10 * max (dot (n, uSun), 0.0) * vec3 (0.5, 0.5, 0.52);
  // The brush grain: a faint streaky modulation.
  float grain = texture (uDetail, vWorld.xz * 0.9 + vWorld.y * vec2 (0.0, 2.7)).b;
  col *= 0.9 + 0.2 * grain;
  // Frost on the upper surfaces, patchy.
  float fr = smoothstep (0.35, 0.8, n.y) * smoothstep (0.25, 0.65, texture (uDetail, vWorld.xz * 0.35 + vSeed).b + 0.25 * n.y);
  vec3 snowC = shadeSnow (vec3 (0.93, 0.95, 0.99), n);
  col = mix (col, snowC, fr);
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 1.0);
}
