#include "ice/light.glsl"

uniform sampler2D uDetail;
uniform vec3 uCamFwd, uCamRight, uCamUp; uniform vec2 uTanHalf; uniform float uShear, uTime, uPxAng;
uniform vec3 uJupR, uJupU, uJupAxis, uIo; uniform float uJupSin, uJupCos, uIoSin;
uniform float uAurora;   // how much Jupiter's aurora and its limb glow, by the guitar: 0 in silence
#include "common/ih.glsl"
#include "common/hcell.glsl"
#include "common/sq.glsl"
in vec2 vNdc;
out vec4 fragColor;
vec3 jupiterColor (float lat, float lon) {
  // Bands by latitude, warped by a little noise along the longitude; the Great Red Spot off the equator.
  float nz = texture (uDetail, vec2 (lon * 0.30, lat * 0.9)).b - 0.5;
  float nz2 = texture (uDetail, vec2 (lon * 0.9 + 0.3, lat * 2.6)).b - 0.5;
  float la = lat + 0.045 * nz + 0.012 * nz2;
  float band = 0.5 + 0.5 * sin (la * 14.0 + 0.6);
  float band2 = 0.5 + 0.5 * sin (la * 31.0 + 1.7);
  vec3 zone = vec3 (0.95, 0.90, 0.80), belt = vec3 (0.56, 0.36, 0.24);
  float bm = smoothstep (0.2, 0.8, band) * 0.7 + 0.3 * band2;
  bm = mix (bm, 0.85, smoothstep (0.08, 0.0, abs (la)));                   // the bright equatorial zone
  vec3 c = mix (belt, zone, bm);
  c = mix (c, vec3 (0.74, 0.62, 0.50), 0.5 * smoothstep (0.35, 0.45, abs (la + 0.12)) * (1.0 - smoothstep (0.45, 0.6, abs (la + 0.12))));   // a dark belt pair
  c = mix (c, vec3 (0.78, 0.70, 0.58), smoothstep (0.9, 1.3, abs (la)));     // paler, bluer poles
  c = mix (c, vec3 (0.62, 0.60, 0.68), smoothstep (1.15, 1.45, abs (la)));
  c *= 0.94 + 0.12 * nz2;
  float e = sq ((lon - 0.35) / 0.21) + sq ((la - 0.36) / 0.085);
  float spot = smoothstep (1.0, 0.55, e);
  c = mix (c, vec3 (0.80, 0.48, 0.36), spot * 0.85);
  c = mix (c, vec3 (0.94, 0.84, 0.70), smoothstep (1.5, 1.0, e) * (1.0 - spot) * 0.6);   // the pale collar
  return c;
}
void main() {
  vec3 dir = normalize (uCamFwd + uCamRight * (vNdc.x * uTanHalf.x) + uCamUp * ((vNdc.y - uShear) * uTanHalf.y));
  vec3 col = vec3 (0.0);
  // The stars: hashed cells in (azimuth, elevation); steady, no twinkle (no air).
  vec2 ae = vec2 (atan (dir.x, -dir.z) * 70.0, asin (clamp (dir.y, -1.0, 1.0)) * 70.0);
  vec2 cf = floor (ae); vec2 f = ae - cf; ivec2 ci = ivec2 (cf);
  float r0 = hcell (ci, 3u);
  if (r0 < 0.16) {
    vec2 sc = vec2 (hcell (ci, 4u), hcell (ci, 5u));
    float dpx = length (f - sc) / 70.0 / uPxAng;       // the distance in pixels
    float mag = hcell (ci, 6u); float br = 0.25 + 1.3 * mag * mag * mag;
    float st = exp (-dpx * dpx * 0.9) * br;
    vec3 sc3 = mix (vec3 (0.75, 0.85, 1.0), vec3 (1.0, 0.92, 0.80), hcell (ci, 7u));
    col += sc3 * st * smoothstep (-0.01, 0.03, dir.y);
  }
  // The sun: a small bright disc with a touch of glare.
  float s = dot (dir, uSun);
  col += vec3 (1.0, 0.97, 0.92) * uSunInt * (smoothstep (0.999970, 0.999985, s) * 2.0 + 0.5 * pow (max (s, 0.0), 1200.0) + 0.05 * pow (max (s, 0.0), 40.0));
  // Jupiter: a shaded sphere in a fixed world direction.
  float cj = dot (dir, uJup);
  if (cj > uJupCos - 0.002) {
    float u = dot (dir, uJupR) / uJupSin, v = dot (dir, uJupU) / uJupSin;
    float rr = u * u + v * v;
    float edge = 1.0 - smoothstep (1.0 - 2.5 * uPxAng / uJupSin, 1.0, rr);
    if (edge > 0.0) {
      float z = sqrt (max (1.0 - rr, 0.0));
      vec3 n = u * uJupR + v * uJupU - z * uJup;      // the surface normal, toward us at the centre
      float lat = asin (clamp (dot (n, uJupAxis), -1.0, 1.0));
      vec3 ax = normalize (cross (uJupAxis, uJup)); vec3 az = cross (uJupAxis, ax);
      float lon = atan (dot (n, ax), dot (n, az)) + uTime * 0.0045;
      vec3 jc = jupiterColor (lat, lon);
      float l = dot (n, uSun);
      float lit = smoothstep (-0.06, 0.34, l) * (0.9 + 0.1 * max (l, 0.0));
      float limb = 0.55 + 0.45 * z;
      vec3 jcol = jc * (lit * 1.25 * limb + 0.045);
      // The aurora: an oval of violet light about each pole, and a glow along the limb.
      float oval = exp (-sq ((abs (lat) - 1.2) / 0.09));
      jcol += vec3 (0.55, 0.50, 1.0) * uAurora * (0.9 * oval + 0.7 * smoothstep (0.4, 0.0, z));
      col = mix (col, jcol, edge);
    }
  }
  // Io: a small yellowish disc, lit by the same sun.
  float cio = dot (dir, uIo);
  if (cio > 0.9995) {
    vec3 ir = normalize (cross (uIo, vec3 (0.0, 1.0, 0.0))); vec3 iu = cross (ir, uIo);
    float u = dot (dir, ir) / uIoSin, v = dot (dir, iu) / uIoSin; float rr = u * u + v * v;
    float edge = 1.0 - smoothstep (1.0 - 3.0 * uPxAng / uIoSin, 1.0 + 1.0 * uPxAng / uIoSin, rr);
    float z = sqrt (max (1.0 - rr, 0.0)); vec3 n = u * ir + v * iu - z * uIo;
    float lit = smoothstep (-0.05, 0.3, dot (n, uSun));
    col = mix (col, vec3 (0.88, 0.80, 0.52) * (lit * 1.0 + 0.02), edge);
  }
  // The ground mist at the horizon: thin, cold, more of it when it snows; below the horizon the haze's colour.
  float mist = exp (-max (dir.y, 0.0) / 0.024) * (0.6 + 0.4 * uSnow);
  col = mix (col, uMist, mist * smoothstep (-0.02, 0.0, dir.y));
  col = mix (col, uHorizon, smoothstep (0.004, -0.01, dir.y));
  fragColor = vec4 (col, 1.0);
}
