// From the prototype's metal-shaders.js (FS_SIGN), its body as it is.
#include "metal/ftex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

uniform sampler2D uAtlas, uBanners;
uniform float uAtlasOn, uBannersOn;
in vec3 vRel; in vec3 vN; in vec2 vQ; in float vPart; in float vFace; in vec4 vRect; in float vCloth;
out vec4 fragColor;
// a banner until the owner's art comes: deep red, a gold border, a steel lightning bolt down the middle
vec4 bannerArt (vec2 q, float seed) {
  vec3 c = vec3 (0.42, 0.03, 0.03) * (0.85 + 0.15 * sin (q.y * 40.0));
  float b = min (min (q.x, 1.0 - q.x), 1.0 - q.y);
  c = mix (c, vec3 (0.85, 0.62, 0.20), 1.0 - smoothstep (0.04, 0.06, b));
  float zig = abs (q.x - 0.5 - 0.12 * (fract (q.y * 3.0 + seed) * 2.0 - 1.0) * step (0.15, q.y) * step (q.y, 0.85));
  c = mix (c, vec3 (0.75, 0.78, 0.82), 1.0 - smoothstep (0.04, 0.06, zig));
  float tail = step (q.y, 0.12) * step (abs (q.x - 0.5), 0.5 - q.y * 4.0);        // a swallowtail cut at the bottom
  return vec4 (c, 1.0 - tail);
}
// a battle jacket until its mesh comes: black denim, patches, studs
vec4 jacket (vec2 q) {
  vec3 c = vec3 (0.05, 0.05, 0.06);
  vec2 g = floor (q * vec2 (3.0, 4.0));
  float h = fract (sin (dot (g, vec2 (12.9898, 78.233))) * 43758.5453);
  vec2 f = fract (q * vec2 (3.0, 4.0));
  float pat = step (0.12, f.x) * step (f.x, 0.88) * step (0.12, f.y) * step (f.y, 0.88) * step (0.35, h);
  c = mix (c, 0.4 + 0.5 * cos (6.2831853 * (vec3 (0.0, 0.33, 0.67) + h * 3.0)), pat * 0.8);
  float stud = 1.0 - smoothstep (0.03, 0.05, length (fract (q * vec2 (12.0, 16.0)) - 0.5) / 4.0);
  c = mix (c, vec3 (0.8), stud * (1.0 - pat));
  return vec4 (c, 1.0);
}
// a sign's face before the atlas comes: a black board, a white rim
vec4 placeholder (vec2 q) {
  float b = min (min (q.x, 1.0 - q.x), min (q.y, 1.0 - q.y));
  return vec4 (mix (vec3 (0.06), vec3 (0.9), 1.0 - smoothstep (0.035, 0.045, b)), 1.0);
}
void main() {
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  bool facing = dot (n, V) < 0.0;
  vec3 wp = vRel + uCamRel;
  if (vPart > 0.5 && abs (vFace) > 0.5) {
    bool front = vFace > 1.5 ? facing : vFace > 0.0;
    vec2 qq = front ? vQ : vec2 (1.0 - vQ.x, vQ.y);
    vec4 t;
    if (vCloth > 0.5) {
      if (vRect.x < -1.5) t = jacket (qq);
      else if (uBannersOn > 0.5 && vRect.x >= 0.0) t = texture (uBanners, vec2 (mix (vRect.x, vRect.z, qq.x), mix (vRect.w, vRect.y, qq.y)));
      else t = bannerArt (qq, vRect.y);
    } else {
      bool place = vRect.x < 0.0 || uAtlasOn < 0.5;
      t = place ? placeholder (qq) : texture (uAtlas, vec2 (mix (vRect.x, vRect.z, qq.x), mix (vRect.w, vRect.y, qq.y)));
    }
    if (t.a < 0.5) discard;
    if (! facing) n = -n;
    vec3 col;
    if (vCloth > 0.5) {
      // the citadel's banners lit by torches at the towers' feet (so the art reads at night, in the small window): warm,
      // brighter low, flickering a little; the flash on top
      vec3 alb = t.rgb * (front ? 1.0 : 0.6);
      float torch = 0.70 + 0.12 * sin (uTime * 6.0 + vQ.x * 3.0) * sin (uTime * 3.7) + 0.25 * (1.0 - vQ.y);
      col = lightMatte (alb, n, wp) + alb * vec3 (1.0, 0.86, 0.70) * torch;
    } else if (front) {
      // A face reads at night (the owner: big, matte faces): lit as by a floodlight of its own, a little of the
      // aurora's hue, the rover's lamps and the flash on top; little haze.
      vec3 light = vec3 (0.62) + uAurAmb * 0.8 + uFlashCol * uFlash * 0.3 + vec3 (0.4) * lampsAt (wp);
      col = t.rgb * min (light, vec3 (1.1));
      fragColor = vec4 (mix (col, uFar, smoothstep (700.0, 1700.0, dist) * 0.5), 0.0);
      return;
    } else col = steel (n, V, wp, 0.5);
    fragColor = vec4 (applyFar (col, dist), 0.0);
    return;
  }
  fragColor = vec4 (applyFar (steel (n, V, wp, 0.7), dist), 0.0);
}
