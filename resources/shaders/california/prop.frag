#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

uniform sampler2D uNoise;
uniform float uKind;
in vec3 vN; in vec3 vRel; in vec3 vLocal; in float vPart; in float vTint; in float vSun; in float vY;
out vec4 fragColor;
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  vec3 col;
  if (vPart > 0.5 && vPart < 1.5 && uKind > 2.5) {
    // the fan's sails: thin galvanised sheet, both sides lit, a glint
    if (dot (n, V) > 0.0) n = -n;
    vec3 alb = vec3 (0.62, 0.62, 0.62);
    float ndl = max ((dot (n, uSun) + 0.3) / 1.3, 0.0);
    vec3 R = reflect (V, n);
    col = alb * (uSunCol * ndl * vSun + mix (uAmbDown, uAmbUp, 0.5 + 0.5 * n.y)) + uSunCol * pow (max (dot (R, uSun), 0.0), 30.0) * 0.8 * vSun;
  } else if (vPart > 0.5 && vPart < 1.5) {
    // The crown: dark olive leaves in clumps, the sun's side warm, a gold-green rim where the sun is behind it.
    float lf = texture (uNoise, vLocal.xz * 0.55 + vLocal.y * 0.31).g;
    float lf2 = texture (uNoise, vLocal.zy * 1.10 + vLocal.x * 0.23).r;
    float clump = smoothstep (0.30, 0.75, lf * 0.6 + lf2 * 0.4);
    n = normalize (n + vec3 (lf - 0.5, lf2 - 0.5, lf * lf2 - 0.25) * 1.6);
    vec3 alb = mix (vec3 (0.15, 0.19, 0.08), vec3 (0.25, 0.28, 0.11), vTint) * (0.55 + 0.75 * clump);
    float ao = 0.55 + 0.45 * smoothstep (0.25, 0.85, vY);
    float ndl = max ((dot (n, uSun) + 0.2) / 1.2, 0.0);
    float rim = pow (1.0 - abs (dot (n, V)), 2.0) * pow (max (dot (V, uSun), 0.0), 2.0);
    col = alb * (uSunCol * ndl * vSun * (0.6 + 0.6 * clump) + mix (uAmbDown, uAmbUp, 0.5 + 0.5 * n.y) * ao);
    col += vec3 (0.85, 0.80, 0.35) * rim * vSun * (0.55 + 0.6 * clump);
  } else if (vPart > 1.5 && vPart < 2.5) {
    col = vec3 (0.55, 0.62, 0.55) * 0.35 + uSunCol * 0.6 * pow (max (dot (V, uSun), 0.0), 20.0);   // the insulators
  } else {
    // Wood: the oak's dark bark, the pole's weathered grey-brown; the windpump's galvanised steel (its vane red);
    // backlit, a thin warm edge.
    vec3 alb = uKind < 0.5 ? vec3 (0.20, 0.16, 0.13) : uKind < 1.5 ? vec3 (0.34, 0.27, 0.21) : vPart > 2.5 ? vec3 (0.60, 0.14, 0.10) : vec3 (0.50, 0.51, 0.53);
    float ndl = max ((dot (n, uSun) + 0.2) / 1.2, 0.0);
    float rim = pow (1.0 - abs (dot (n, V)), 3.0) * pow (max (dot (V, uSun), 0.0), 3.0);
    col = alb * (uSunCol * ndl * vSun + mix (uAmbDown, uAmbUp, 0.5 + 0.5 * n.y)) + uSunCol * rim * 0.35 * vSun;
  }
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 0.0);
}
