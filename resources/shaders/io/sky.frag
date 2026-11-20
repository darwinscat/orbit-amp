// From the prototype's io-shaders.js (FS_SKY), its body as it is.
#include "io/ftex.glsl"
#include "io/common.glsl"
#include "io/light.glsl"

uniform sampler2D uNoise;
uniform vec3 uCamFwd, uCamRight, uCamUp; uniform vec2 uTanHalf; uniform float uShear, uPxAng;
uniform vec3 uJupR, uJupU, uJupAxis; uniform float uJupSin, uJupCos, uJupT, uSep, uRho;
uniform vec4 uMoon[2]; uniform vec3 uMoonAlb[2];
uniform float uAurora, uFlux;          // the attack's aurora on Jupiter (a flash and a glow), the flux tube's footprint
const float AUR_DAY = 2.2;             // the aurora's lift where the disc is sunlit (so a hit reads on a full Jupiter by day)
const float RING_K = 0.2;              // the rings' hairline in the eclipse, as faint as in life (20.11, the owner: it read as two sticks)
uniform vec3 uJupSunCol;               // the Sun's light where Jupiter and the moons are (not through Jupiter's haze)
uniform float uJupK;                   // Jupiter's sunlit disc against the exposure's lift (so a full Jupiter at night keeps its bands)
uniform float uFlat, uAir;             // the Sun's disc flattened in the haze (0..1); Io's airglow (0..1)
uniform vec3 uBandH, uBandT, uBandN;   // the Milky Way's frame
uniform vec3 uFar;                     // the plain far off (under the horizon)
uniform vec3 uGKey, uGKeyCol, uGAmb;   // the surface's light (the floor in the dark: io-gl.js surfaceLight), for the massifs
uniform vec3 uSunUp;                   // the Sun's disc over the skyline from the eye, its glare's (both with uSunK), the
                                       // glare's without Jupiter's cover (the night's gate in it: the corona, the contact's flash)
in vec2 vNdc;
out vec4 fragColor;
float nz (vec2 uv, int ch) { vec4 t = textureLod (uNoise, uv, 0.0); return ch == 0 ? t.r : ch == 1 ? t.g : ch == 2 ? t.b : t.a; }
// Jupiter's clouds by latitude (radians, + north) and longitude: the zones cream, the belts tan to rust-brown, the
// equatorial zone with its blue-grey festoons, the Great Red Spot in the south, white ovals, the poles grey and mottled.
// Each band drifts at its own rate (the jets alternate), slowly.
vec3 jupiterColour (float lat, float lon, float T) {
  float L = lat * 57.29578;
  float drift = T * (0.0040 + 0.0030 * sin (lat * 13.0 + 0.6) + 0.0015 * sin (lat * 29.0));
  float lo = lon + drift;
  vec4 t1 = textureLod (uNoise, vec2 (lo * 0.159 * 2.0, lat * 0.70 + 0.31), 0.0);
  vec4 t2 = textureLod (uNoise, vec2 (lo * 0.159 * 6.0 + 0.4, lat * 2.4 + 0.77), 0.0);
  float la = L + 2.2 * (t1.r - 0.5) + 0.9 * (t2.g - 0.5) * (1.0 + smoothstep (10.0, 40.0, abs (L)));
  // belts (1) and zones (0)
  float belt = 0.0;
  // (20.11, the owner: "juicier, more contrast, the spot redder, the bands sharper": the colours of Juno's and Hubble's
  // enhanced views, the band edges a degree wide instead of two and a half)
  belt += smoothstep (6.6, 7.8, la) * (1.0 - smoothstep (17.6, 18.8, la));                    // the North Equatorial Belt
  belt += smoothstep (6.4, 7.6, -la) * (1.0 - smoothstep (19.6, 20.8, -la)) * 0.95;            // the South Equatorial Belt (its edge was signed wrong: the EZ was belt-coloured)
  belt += 0.62 * smoothstep (23.5, 24.6, la) * (1.0 - smoothstep (29.4, 30.5, la));            // the North Temperate Belt
  belt += 0.52 * smoothstep (27.5, 28.6, -la) * (1.0 - smoothstep (33.4, 34.5, -la));          // the South Temperate Belt
  belt += 0.36 * smoothstep (35.5, 36.6, abs (la)) * (1.0 - smoothstep (40.4, 41.5, abs (la)));
  belt = clamp (belt, 0.0, 1.0);
  vec3 zone = vec3 (0.98, 0.94, 0.84), tanC = vec3 (0.80, 0.58, 0.40), rust = vec3 (0.58, 0.32, 0.18);
  float hue = 0.5 + 0.5 * sin (la * 0.21 + 1.3) + 0.4 * (t2.b - 0.5);
  vec3 bc = mix (tanC, rust, clamp (hue, 0.0, 1.0));
  bc = mix (bc, vec3 (0.36, 0.21, 0.12), smoothstep (11.0, 17.0, la) * smoothstep (0.0, 1.0, belt) * 0.7);    // the NEB's dark barges
  vec3 c = mix (zone, bc, belt);
  // the equatorial zone: festoons, blue-grey plumes trailing off the NEB's south edge
  float fest = smoothstep (0.62, 0.80, textureLod (uNoise, vec2 (lo * 0.159 * 10.0, la * 0.05 + 0.2), 0.0).b) * exp (-sq ((la - 4.0) / 2.5));
  c = mix (c, vec3 (0.34, 0.40, 0.52), fest * 0.7);
  // high latitudes: greyer and a little blue, mottled, darker toward the poles
  float polar = smoothstep (42.0, 62.0, abs (la));
  c = mix (c, vec3 (0.60, 0.61, 0.64) * (0.85 + 0.3 * t2.r), polar);
  c *= 0.90 + 0.16 * (t2.g - 0.5) * 2.0;
  // the Great Red Spot (22 S): brick-orange, its core deeper red, the swirl through it; its pale collar (the hollow it
  // sits in, cut into the SEB); white ovals farther south
  // (20.11: the spot and the ovals ride on their own steady drift, not on the bands' latitude-dependent one, which
  // sheared the spot into a chevron as the time grew; the rates are what the bands gave at their centres)
  float gl = lon - 0.9 - T * 0.0073;
  gl -= TAU * floor (gl / TAU + 0.5);
  float e = sq (gl / 0.235) + sq ((L + 22.0) / 6.5);
  float swirl = textureLod (uNoise, vec2 (gl * 1.5 + 0.5, (L + 22.0) * 0.08 + e * 0.15), 0.0).g;
  vec3 grs = mix (vec3 (0.88, 0.42, 0.24), vec3 (0.70, 0.22, 0.12), (1.0 - smoothstep (0.0, 0.6, e)) * 0.7) * (0.86 + 0.28 * swirl);
  c = mix (c, vec3 (0.99, 0.95, 0.86), smoothstep (1.7, 1.0, e) * smoothstep (0.7, 1.05, e) * 0.75);
  c = mix (c, grs, (1.0 - smoothstep (0.6, 1.0, e)) * 0.96);
  // a touch more colour over all (the enhanced views'), the zones kept cream
  c = clamp (mix (vec3 (dot (c, vec3 (0.30, 0.55, 0.15))), c, 1.18), 0.0, 1.0);
  for (int k = 0; k < 3; ++k) {
    float ol = lon - 2.1 - 1.3 * float (k) - T * 0.0012; ol -= TAU * floor (ol / TAU + 0.5);
    float eo = sq (ol / 0.06) + sq ((L + 33.5) / 1.6);
    c = mix (c, vec3 (0.97, 0.95, 0.92), (1.0 - smoothstep (0.4, 1.0, eo)) * 0.8);
  }
  return c;
}
// a moon: a small lit disc (dir, sin of its angular radius)
vec3 moonDisc (vec3 dir, vec4 M, vec3 alb, inout float cover) {
  float cm = dot (dir, M.xyz);
  float s = M.w;
  if (cm < sqrt (1.0 - s * s) - 0.0005) return vec3 (0.0);
  vec3 r = normalize (cross (M.xyz, vec3 (0.0, 1.0, 0.0))), u = cross (r, M.xyz);
  float x = dot (dir, r) / s, y = dot (dir, u) / s, rr = x * x + y * y;
  float edge = 1.0 - smoothstep (1.0 - 1.5 * uPxAng / s, 1.0 + 0.8 * uPxAng / s, rr);
  if (edge <= 0.0) return vec3 (0.0);
  float z = sqrt (max (1.0 - rr, 0.0));
  vec3 n = x * r + y * u - z * M.xyz;
  float lit = smoothstep (-0.04, 0.20, dot (n, uSun)) * uJupSunCol.r / 2.1;
  cover = max (cover, edge);
  return alb * (lit * 1.05 * (0.6 + 0.4 * z)) * edge;
}
void main() {
  vec3 dir = normalize (uCamFwd + uCamRight * (vNdc.x * uTanHalf.x) + uCamUp * ((vNdc.y - uShear) * uTanHalf.y));
  float az = atan (dir.x, -dir.z) / TAU;
  float el = asin (clamp (dir.y, -1.0, 1.0));
  // THE FAR MASSIFS over the horizon (Io's mountains stand alone: two layers of broad peaks, 6 and 14 km off, the
  // ground falling away under them with the curvature), lit by the Sun on the faces turned to it
  for (int k = 0; k < 2; ++k) {
    if (el > 0.13) break;
    float D = k == 0 ? 6000.0 : 14000.0;
    float u0 = az * (k == 0 ? 3.0 : 2.0) + 0.37 * float (k);
    float hA = nz (vec2 (u0, 0.81 + 0.07 * float (k)), 0), hB = nz (vec2 (u0 * 4.0, 0.33 + 0.07 * float (k)), 1);
    float peak = smoothstep (0.55, 0.95, hA);
    float H = (k == 0 ? 40.0 + 520.0 * peak * peak : 80.0 + 1600.0 * peak * peak) * (0.85 + 0.3 * hB);
    float top = atan ((H - uCamRel.y - D * D * 0.5 / IO_R) / D);
    if (el < top && dir.y > -0.02) {
      float dA = nz (vec2 (u0 + 0.004, 0.81 + 0.07 * float (k)), 0) - hA;
      float side = clamp (dA * 60.0, -1.0, 1.0);
      vec3 dh = normalize (vec3 (dir.x, 0.0, dir.z)), rt = vec3 (-dh.z, 0.0, dh.x);
      vec3 nn = normalize (-dh * 0.75 + vec3 (0.0, 0.55, 0.0) - rt * side * 0.6);
      vec3 alb = mix (vec3 (0.66, 0.56, 0.34), vec3 (0.80, 0.74, 0.55), hB) * (k == 0 ? 1.0 : 0.92);
      // the surface's light, Io's own shadow at the massif's point (their crowns keep the Sun last)
      vec3 pm = uCamRel + vec3 (dh.x, 0.0, dh.z) * D; pm.y = uCamRel.y + D * tan (el) + D * D * 0.5 / IO_R;
      vec3 c = alb * (uGKeyCol * max (dot (nn, uGKey), 0.0) * ioTerm (pm) + uGAmb * 0.6 + uAirglow * 0.6);
      fragColor = vec4 (mix (c, uFar, smoothstep (0.0, -0.02, dir.y)), 1.0);
      return;
    }
  }
  if (dir.y < -0.0018) { fragColor = vec4 (uFar, 1.0); return; }
  vec3 col = vec3 (0.0);
  float day = 1.0 - uDark;                                     // the eye set to the sunlit ground by day
  // the Milky Way, faint (fainter still by day: the exposure is the sunlit ground's)
  float blat = asin (clamp (dot (dir, uBandN), -1.0, 1.0));
  float blon = atan (dot (dir, uBandT), dot (dir, uBandH)) / TAU;
  float halo = exp (-sq (blat / 0.20));
  if (halo > 0.01 && day < 0.9) {
    float core = exp (-sq (blat / 0.075));
    vec4 cl = textureLod (uNoise, vec2 (blon * 6.0, blat * 2.3 + 0.5), 0.0);
    float cloud = cl.r, cloud2 = cl.b;
    float lane = smoothstep (0.45, 0.7, cl.g) * exp (-sq ((blat + 0.015) / 0.045));
    float mw = (core * 0.8 + halo * 0.35) * (0.5 + 0.8 * cloud) * (1.0 - 0.6 * lane);
    col += mix (vec3 (0.56, 0.62, 0.84), vec3 (0.80, 0.74, 0.66), smoothstep (0.35, 0.75, cloud2)) * mw * 0.075 * (1.0 - 0.6 * day);
  }
  // the stars: three hashed layers of cells in (azimuth, elevation), steady
  for (int l = 0; l < 2; ++l) {
    if (l == 1 && day > 0.9) break;                            // by day only the brighter stars
    float N = (l == 0) ? 420.0 : 1100.0;
    float prob = (l == 0) ? 0.07 : 0.14;
    vec2 ae = vec2 (az + 0.5, el / TAU + 0.5) * N;
    vec2 cf = floor (ae); vec2 f = ae - cf;
    ivec2 ci = ivec2 (int (cf.x) % int (N), int (cf.y));
    uint ls = uint (l) * 8u;
    float cellPx = (TAU / N) / uPxAng;
    float keep = smoothstep (2.5, 6.0, cellPx) * (l == 0 ? 1.0 : 1.0 - 0.7 * day);
    if (hcell (ci, 3u + ls) < prob * (1.0 + 1.3 * halo) * keep) {
      vec2 sc = vec2 (hcell (ci, 4u + ls), hcell (ci, 5u + ls));
      float dpx = length (f - sc) * (TAU / N) / uPxAng;
      float mag = hcell (ci, 6u + ls);
      float br = (l == 0 ? 0.35 + 1.6 * mag * mag * mag : 0.08 + 0.45 * mag * mag) * (1.0 - 0.45 * day);
      col += mix (vec3 (0.70, 0.82, 1.0), vec3 (1.0, 0.86, 0.68), hcell (ci, 7u + ls)) * exp (-dpx * dpx * 0.75) * br;
    }
  }
  // the Sun: a twentieth of a degree, very bright; the lens's glare round it. Through Jupiter's haze it dims and reddens
  // (uSunCol is what reaches Io) and its disc and glare flatten across the limb
  float cs = dot (dir, uSun);
  float th = sqrt (max (2.0 - 2.0 * cs, 0.0));
  vec3 sunC = vec3 (0.0);
  if (uSunUp.x > 0.0 || uSunUp.y > 0.0) {         // (both 0 with the Sun under the skyline or gated by the night: no disc, no glare)
    vec3 e = dir - uSun * cs, radial = normalize (uJup - uSun * dot (uJup, uSun) + vec3 (1e-6));
    float er = dot (e, radial), et = length (e - radial * er);
    float tha = sqrt (et * et + sq (er / (1.0 - 0.7 * uFlat)));
    sunC = uSunCol * (smoothstep (0.0011, 0.0006, tha) * 40.0 * uSunUp.x + (exp (-tha / 0.0045) * 2.0 + exp (-tha / 0.03) * 0.12 + exp (-th / 0.2) * 0.03) * uSunUp.y);
  }
  // JUPITER
  float cj = dot (dir, uJup);
  float cover = 0.0;
  vec3 jcol = vec3 (0.0);
  if (cj > uJupCos - 0.03) {
    float u = dot (dir, uJupR) / uJupSin, v = dot (dir, uJupU) / uJupSin;
    float rr = u * u + v * v, r = sqrt (rr);
    float pxN = uPxAng / uJupSin;                      // a pixel in units of Jupiter's radius
    float edge = 1.0 - smoothstep (1.0 - 1.2 * pxN, 1.0 + 0.6 * pxN, r);
    vec2 sp = vec2 (dot (uSun, uJupR), dot (uSun, uJupU));
    vec2 spd = length (sp) > 1e-5 ? normalize (sp) : vec2 (-1.0, 0.0);
    if (edge > 0.0) {
      float z = sqrt (max (1.0 - rr, 0.0));
      vec3 n = u * uJupR + v * uJupU - z * uJup;
      float lat = asin (clamp (dot (n, uJupAxis), -1.0, 1.0));
      vec3 ax = normalize (cross (uJupAxis, uJup)), azv = cross (uJupAxis, ax);
      float lon = atan (dot (n, ax), dot (n, azv));
      vec3 jc = jupiterColour (lat, lon, uJupT);
      float l = dot (n, uSun);
      // a soft terminator (the deep atmosphere), the limb darkened, a reddening near the terminator
      float lit = smoothstep (-0.05, 0.28, l);
      float limb = 0.5 + 0.5 * sqrt (max (z, 0.0));
      vec3 term = mix (vec3 (1.0, 0.72, 0.55), vec3 (1.0), smoothstep (0.0, 0.35, l));
      jcol = jc * term * uJupSunCol * lit * limb * 0.95 * uJupK + jc * 0.0035;
      // the aurora: an oval of violet-white light about each pole, the Io footprint's spot, when the flux tube fires
      float oval = exp (-sq ((abs (lat) - 1.20) / 0.075)) * (0.65 + 0.35 * nz (vec2 (lon * 0.5, lat), 2));
      float cap = smoothstep (1.0, 1.35, abs (lat));
      float foot = exp (-(sq ((lat - 1.10) / 0.03) + sq ((lon - 0.55) / 0.08)));
      float dayK = 1.0 + AUR_DAY * lit;               // where the disc is sunlit it has the lit cloud tops to beat
      jcol += vec3 (0.42, 0.40, 1.0) * uAurora * dayK * (2.6 * oval + 0.9 * cap + 0.35 * smoothstep (0.35, 0.0, z) * step (0.0, dot (n, uJupAxis)));
      jcol += vec3 (0.75, 0.85, 1.0) * uFlux * 3.0 * foot;
      // lightning on the night side, now and then (rare faint flickers, as Juno saw them)
      float storm = step (0.993, fract (sin (floor (uTime * 1.7) * 12.9898) * 43758.5453));
      vec2 lp = vec2 (fract (sin (floor (uTime * 1.7) * 7.1) * 911.3) - 0.5, fract (sin (floor (uTime * 1.7) * 3.3) * 377.1) - 0.5) * 1.3;
      jcol += vec3 (0.8, 0.85, 1.0) * storm * 0.25 * exp (-dot (vec2 (u, v) - lp, vec2 (u, v) - lp) / 0.0012) * (1.0 - lit);
      cover = edge;
    }
    // the aurora standing above the limb over the poles (its glow is high in the atmosphere): an arc over each, the north's
    // brighter (it is the one turned a little toward us)
    if (uAurora > 0.01 && r > 0.98) {
      float pv = dot (vec2 (u, v) / max (r, 1e-4), normalize (vec2 (dot (uJupAxis, uJupR), dot (uJupAxis, uJupU))));
      float arc = exp (-max (r - 1.0, 0.0) / 0.035) * smoothstep (0.80, 0.98, abs (pv)) * (pv > 0.0 ? 1.0 : 0.6);
      col += vec3 (0.42, 0.40, 1.0) * uAurora * (1.0 + 1.8 * day) * arc * 1.6 * (1.0 - cover);
    }
    // THE SUNSET AND THE ECLIPSE. As the Sun nears the limb its light lights Jupiter's upper haze there (an arc growing on
    // the limb by it); at ingress the ring of sunlight bent and scattered through the haze is one-sided — a big bright arc
    // at the Sun's spot, the diamond ring, its glow blooming far wider than the ring is (a bright arc on black does that
    // in the eye), a small pearly solar corona peeking past the limb, the far side faint; the rings' edge-on hairline lit
    // from behind, brightest by the Sun. Deeper the arc weakens and spreads round, the ring evens out and dims: at the
    // end thin, even and dim.
    float behind = uRho - uSep;                                       // >0: the Sun behind the limb (radians)
    float near = smoothstep (uRho + 0.06, uRho, uSep);                // the last ~3.4 degrees before contact, and after
    if (near > 0.0) {
      float dep = clamp (behind / 0.0803, 0.0, 1.0);                  // how deep behind (to 4.6 degrees)
      float onset = smoothstep (-0.002, 0.003, behind);               // the ring proper, from contact on
      float pa = dot (vec2 (u, v) / max (r, 1e-4), spd);
      float lobe = pow (0.5 + 0.5 * pa, mix (7.0, 1.3, sqrt (dep)));
      float w = (1.6 * pxN + 0.0022) * (1.0 + 1.4 * (1.0 - dep) * lobe * onset);
      float ringP = exp (-sq ((r - 1.0 - 0.3 * w) / w));
      float ring = ringP * (onset * (mix (7.0, 0.18, sqrt (dep)) * lobe + mix (0.20, 0.28, dep)) * mix (1.0, 0.35, dep) + (1.0 - onset) * near * near * 2.5 * lobe);
      vec3 rc = mix (vec3 (1.0, 0.30, 0.10), vec3 (1.0, 0.86, 0.64), lobe * (1.0 - 0.6 * dep));
      float haze = exp (-max (r - 1.0, 0.0) / mix (0.09, 0.025, dep)) * step (1.0, r) * (mix (1.1, 0.04, sqrt (dep)) * lobe * near + 0.04 * onset);
      float plane = dot (dir, uJupAxis);
      float rings = exp (-sq (plane / max (uPxAng * 0.8, 1e-5))) * smoothstep (1.0, 1.04, r) * ((1.0 - smoothstep (1.76, 1.84, r)) * 0.20 + (1.0 - smoothstep (1.9, 2.6, r)) * 0.05) * (0.3 + lobe * (1.0 + 5.0 * (1.0 - dep))) * near * RING_K;
      // the solar corona: pearly, streamered, round the Sun's place, past the limb only, gone as the Sun goes deeper
      vec3 ts = normalize (cross (uSun, vec3 (0.0, 1.0, 0.0))), us = cross (ts, uSun);
      float sa = atan (dot (dir, us), dot (dir, ts));
      float strm = 0.6 + 0.4 * sin (sa * 5.0 + 1.3) * sin (sa * 3.0);
      float corona = (0.9 * exp (-th / 0.010) + 0.5 * exp (-th / 0.024) * strm) * (1.0 - smoothstep (0.0, 0.035, behind)) * smoothstep (-0.004, 0.001, behind) * 1.4 * uSunUp.z;
      col += (rc * ring + vec3 (1.0, 0.64, 0.42) * haze + vec3 (0.95, 0.85, 0.75) * rings + vec3 (0.92, 0.94, 1.0) * corona) * (1.0 - cover);
    }
  }
  col = mix (col + sunC, jcol, cover);
  {
    float behind = uRho - uSep, dep = clamp (behind / 0.0803, 0.0, 1.0);
    float ingress = smoothstep (-0.003, 0.0005, behind) * (1.0 - smoothstep (0.0, 0.03, behind));
    if (ingress * uSunUp.z > 0.0) col += vec3 (1.0, 0.82, 0.60) * ingress * (3.5 * exp (-th / 0.005) + 1.3 * exp (-th / 0.02) + 0.35 * exp (-th / 0.07)) * (1.0 - 0.7 * dep) * uSunUp.z;
  }
  // Europa and Ganymede
  float mc = 0.0;
  vec3 m0 = moonDisc (dir, uMoon[0], uMoonAlb[0], mc), m1 = moonDisc (dir, uMoon[1], uMoonAlb[1], mc);
  col = mix (col, m0 + m1, mc);
  // Io's airglow in the dark (night or eclipse): its thin atmosphere glowing dimly red and green low over the horizon
  col += uAir * (vec3 (0.050, 0.008, 0.006) * exp (-max (el, 0.0) / 0.09) + vec3 (0.006, 0.022, 0.010) * exp (-max (el, 0.0) / 0.025));
  fragColor = vec4 (col, 1.0);
}
