in vec3 vCol; in float vA; in vec2 vLoc; in float vLen; in float vRad; in float vSoft;
uniform float uExposure;
out vec4 o;
#include "titan/hash.glsl"
#include <world>
#include "titan/util.glsl"

void main(){
  float dx = vLoc.x - clamp(vLoc.x, 0.0, vLen);
  float dist = length(vec2(dx, vLoc.y));
  float cover = mix(clamp(vRad + 0.5 - dist, 0.0, 1.0), sq(max(1.0 - dist / (vRad + 1.0), 0.0)), vSoft);   // drops crisp, mist soft
  float bead = 1.0 + 0.6 * (1.0 - vSoft) * (1.0 - smoothstep(0.0, vRad, dist));   // a bright core: a round drop
  float a = min(1.0, vA * cover);
  vec3 c = toDisplay(vCol * bead * uExposure);
  o = vec4(c * a, a);
}
