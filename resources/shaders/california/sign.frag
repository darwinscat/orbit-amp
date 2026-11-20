#define HTEX(uv) texture(uHeight, uv)
#define RTEX(uv) texture(uRoad, uv)
#define LTEX(uv) texture(uLand, uv)
#include "california/common.glsl"
#include "california/light.glsl"

uniform sampler2D uAtlas, uFlags, uNoise;
uniform float uAtlasOn, uFlagsOn;
in vec3 vRel; in vec3 vN; in vec2 vQ; in float vPart; in float vFace; in vec4 vRect; in float vSun; in float vCloth;
out vec4 fragColor;
// a painted placeholder face (until the atlas comes): a cream board, a red border, a faded sunburst
vec4 placeholder (vec2 q) {
  float b = min (min (q.x, 1.0 - q.x), min (q.y, 1.0 - q.y));
  vec3 c = vec3 (0.94, 0.88, 0.74);
  vec2 d = q - vec2 (0.5, 0.45);
  c = mix (c, vec3 (0.98, 0.62, 0.22), step (0.5, fract (atan (d.y, d.x) * 2.546)) * smoothstep (0.45, 0.1, length (d)) * 0.6);
  c = mix (c, vec3 (0.85, 0.30, 0.16), 1.0 - smoothstep (0.035, 0.045, b));
  return vec4 (c, 1.0);
}
// a tie-dyed cloth (until the flags' atlas comes): a rainbow spiral from a knot, its seed's own
vec4 tieDye (vec2 q, float seed) {
  vec2 d = (q - vec2 (0.3 + 0.4 * seed, 0.5)) * vec2 (1.4, 1.0);
  float a = atan (d.y, d.x) * 0.159155 + length (d) * 2.6 + seed * 3.0;
  return vec4 (0.55 + 0.45 * cos (6.2831853 * (vec3 (0.0, 0.33, 0.67) + a)), 1.0);
}
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  bool facing = dot (n, V) < 0.0;
  vec3 alb = vec3 (0.46, 0.39, 0.31);               // weathered wood
  bool wood = true, face = false;
  float transl = 0.0;
  if (vPart > 2.5) { alb = vRect.rgb; wood = false; }   // a stand-in's plain paint
  else if (vPart > 0.5 && abs (vFace) > 0.5) {
    bool front = vFace > 1.5 ? facing : vFace > 0.0;
    vec2 qq = front ? vQ : vec2 (1.0 - vQ.x, vQ.y);
    vec4 t;
    if (vCloth > 0.5) {
      t = (uFlagsOn > 0.5 && vRect.x >= 0.0) ? texture (uFlags, vec2 (mix (vRect.x, vRect.z, qq.x), mix (vRect.y, vRect.w, 1.0 - qq.y))) : tieDye (qq, vRect.y);
      transl = 0.9;
    } else {
      bool place = vRect.x < 0.0 || uAtlasOn < 0.5;
      t = place ? placeholder (qq) : texture (uAtlas, vec2 (mix (vRect.x, vRect.z, qq.x), mix (vRect.w, vRect.y, qq.y)));
      if (vFace > 1.5) transl = 0.6;
    }
    if (t.a < 0.5) discard;
    if (front || vCloth > 0.5) { alb = t.rgb * (front ? 1.0 : 0.8); wood = false; face = vCloth < 0.5; }
    else if (vFace > 1.5) { alb = mix (vec3 (0.86, 0.82, 0.74), t.rgb, 0.25); wood = false; face = true; }   // a banner's back: the print through the cloth
    if (! facing) n = -n;
  }
  if (wood) {
    float grain = texture (uNoise, vec2 (dot (vRel.xz, vec2 (0.7, 0.7)) * 0.05 + vRel.y * 0.9, vRel.y * 0.08)).g;
    alb *= 0.78 + 0.44 * grain;
  }
  if (face) {
    // A sign's face reads whatever the sun does (the owner, 18.11: backlit by the low sun the Heaven board glared
    // pale): matte, lit mostly by the sky it looks at (its hue a little, not its brightness) over a floor, a little
    // of the sun on top; little haze; marked in the alpha so the sun's rays leave it alone (the post pass).
    vec3 skyN = skyColour (normalize (vec3 (n.x, max (n.y, 0.0) + 0.3, n.z)));
    vec3 tint = skyN / max (max (skyN.r, skyN.g), max (skyN.b, 1e-3));
    vec3 light = vec3 (0.50) + 0.40 * mix (vec3 (1.0), tint, 0.3) + uSunCol * 0.16 * vSun * max (dot (n, uSun), 0.0);
    vec3 col = alb * min (light, vec3 (1.05));
    col = mix (col, hazeColour (V), hazeAmount (dist) * 0.3);
    fragColor = vec4 (col, 0.25);
    return;
  }
  vec3 col = lightGround (alb, n, V, vSun, transl) + alb * (wood ? 0.10 : 0.30 + 0.25 * max (-dot (n, uSun), 0.0));
  col = applyHaze (col, dist, V);
  fragColor = vec4 (col, 0.0);
}
