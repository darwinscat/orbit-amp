uniform sampler2D uNoise;
uniform vec3 uHR, uHF; uniform float uTh, uCamY, uDuneOff;
uniform vec2 uLand0, uLand1, uLand2;
out vec4 o;
#include <world>
#include "titan/util.glsl"
#include <profile>
// height of the land above the sea (m) at camera-relative xz p, D = its distance; duneW = share of dune field
float landH(vec2 p, float D, out float duneW){
  vec4 a = textureLod(uNoise, uLand0 + p / LA, 0.0);
  vec4 b = textureLod(uNoise, uLand1 + p / LB, 0.0);
  float n = 0.72 * a.r + 0.28 * b.g;
  float coast = smoothstep(C0, C1, n);
  duneW = 0.0;
  if (coast <= 0.0) return 0.0;
  vec4 c = textureLod(uNoise, uLand2 + p / LC, 0.0);
  float inland = smoothstep(C0 + 0.02, C1 + 0.12, n);
  duneW = smoothstep(0.42, 0.62, a.g) * inland;
  float hills = 25.0 + inland * (250.0 * c.r * c.r + 80.0 * c.b * c.b) + 60.0 * (b.a - 0.3);   // low, rounded
  float s = dot(p, DUNE_N) / DUNE_P + uDuneOff + (b.r - 0.5) * 1.4;
  float dune = pow(0.5 + 0.5 * sin(6.2831853 * s), 3.0) * 115.0;
  float h = coast * (mix(hills, 16.0 + 0.35 * hills, duneW) + dune * duneW);
  return h * smoothstep(NEAR0, NEAR1, D);
}
float bandMax(int k, vec2 dir, out float bestD, out float bestW){
  float best = -1.0; bestD = BMIN[k]; bestW = 0.0;
  float r = BMAX[k] / BMIN[k];
  for (int i = 0; i < BAND_STEPS; i++) {
    float D = BMIN[k] * pow(r, (float(i) + 0.5) / BAND_STEPS_F);
    float w; float h = landH(dir * D, D, w);
    float tn = (h - uCamY - D * D / (2.0 * R_T)) / D;
    if (tn > best) { best = tn; bestD = D; bestW = w; }
  }
  return best;
}
vec2 enc16(float v){ v = clamp(v, 0.0, 1.0) * 255.0; float hi = floor(v); return vec2(hi / 255.0, v - hi); }
void main(){
  int k = int(gl_FragCoord.y);
  float xs = mix(PROF_X0, PROF_X1, gl_FragCoord.x / COLS_F);
  vec2 dir = normalize(uHF.xz + (xs * uTh) * uHR.xz);
  float D = 0.0, w = 0.0, best = -1.0, u = 0.0;
  if (k < 5) { best = bandMax(k, dir, D, w); u = (D - BMIN[k]) / (BMAX[k] - BMIN[k]); }
  else { for (int j = 0; j < 5; j++) { float d2, w2; best = max(best, bandMax(j, dir, d2, w2)); } }
  o = vec4(enc16((best - TAN_MIN) / (TAN_MAX - TAN_MIN)), u, w);
}
