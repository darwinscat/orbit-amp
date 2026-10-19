in vec2 vUv; flat in int vPart; flat in int vType; in vec3 vCol; in float vA; in float vDist;
uniform float uTime, uExposure; uniform vec3 uAmb, uHaze;
out vec4 o;
#include "titan/hash.glsl"
#include <world>
#include "titan/util.glsl"

void main(){
  if (vPart == 0) {
    float y = vUv.y * 2.2 - 0.05, x = abs(vUv.x) * 0.6;                // metres above the waterline / off the axis
    float aw = max(fwidth(x), 0.004), ah = max(fwidth(y), 0.004);
    float hull = (1.0 - smoothstep(-aw, aw, x - (0.38 - 0.12 * clamp(y / 0.6, 0.0, 1.0)))) * (1.0 - smoothstep(-ah, ah, y - 0.6)) * smoothstep(-ah, ah, y + 0.02);
    float mast = (1.0 - smoothstep(-aw, aw, x - (0.05 + 0.13 * clamp((1.72 - y) / 1.1, 0.0, 1.0)))) * smoothstep(-ah, ah, y - 0.58) * (1.0 - smoothstep(-ah, ah, y - 1.72));
    mast *= 0.55 + 0.45 * step(0.5, fract(y * 6.0 + x * 9.0));          // a lattice, not a pole
    float mark = 1.0 - smoothstep(-aw, aw, length(vec2(x, (y - 1.8) * 1.2)) - 0.12);
    float lamp = 1.0 - smoothstep(-aw, aw, length(vec2(x, y - 1.95)) - 0.075);
    float a = max(max(hull, mast), max(mark, lamp));
    vec3 paint = vType == 0 ? vec3(0.32, 0.05, 0.04) : (vType == 1 ? vec3(0.04, 0.2, 0.09) : vec3(0.36, 0.24, 0.04));
    vec3 c = paint * (0.35 + 0.65 * smoothstep(-0.1, 0.6, y)) + uAmb * 0.05 * smoothstep(0.2, 0.0, abs(vUv.x) - 0.6);
    c += vec3(0.25, 0.22, 0.24) * (1.0 - smoothstep(0.0, 0.05, abs(y))) * step(x, 0.42);   // a lick of froth at the waterline
    c = mix(c, vCol * 3.0 * vA + 0.05, lamp);
    c = mix(c, uHaze, 1.0 - exp(-vDist / 5000.0));
    vec3 d = toDisplay(c * uExposure);
    o = vec4(d * a, a);
  } else if (vPart == 1) {
    float r2 = dot(vUv, vUv);
    vec3 c = vCol * vA * (exp(-r2 * 10.0) * 1.6 + exp(-r2 * 3.0) * 0.3) * sq(max(1.0 - r2, 0.0));   // fades to nothing at the sprite's edge
    o = vec4(toDisplay(c * uExposure), 0.0);                            // additive
  } else {
    float across = exp(-vUv.x * vUv.x * 3.0) * sq(max(1.0 - vUv.x * vUv.x, 0.0));
    float along = exp(-vUv.y * vUv.y * 1.3) * smoothstep(-1.0, -0.6, vUv.y);
    float glit = h01(uint(gl_FragCoord.y * 0.5) + 77u, uint(uTime * 9.0)) * 1.4 + 0.15;
    vec3 c = vCol * vA * across * along * glit * 0.55;
    o = vec4(toDisplay(c * uExposure), 0.0);
  }
}
