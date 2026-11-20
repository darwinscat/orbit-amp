// From the prototype's metal-shaders.js (LIGHT), its body as it is.
uniform vec3 uAurAmb, uStarAmb, uFar;   // the aurora's light from above, the stars', the far plain's colour
uniform float uStorm, uFlash, uEnvHot;  // the storm (0..1), the lightning's flash now, how hot the plain round the van is
uniform vec3 uFlashCol;
uniform vec2 uHitT;                     // the last attack's age (s) and strength
uniform vec4 uPL[2];                    // two fires lighting what is near: position (rel), strength
uniform vec4 uLamps;                    // the rover's lamps: its place (rel xz), its way (xz)
uniform vec4 uTunnel;                   // the tunnel: its angles round the loop (from, to), its half width, 1 on
float hitGlow () { if (uHitT.y <= 0.0 || uHitT.x > 9.0) return 0.0; return uHitT.y * (1.0 * exp (-uHitT.x / 0.2) + 0.6 * exp (-uHitT.x / 1.6)); }
// how far inside the tunnel (1 inside, 0 out), from the loop's angle and the road distance
float inTunnel (vec2 pr, float ad) {
  if (uTunnel.w < 0.5) return 0.0;
  vec2 lq = pr - uLoopC;
  float th = atan (lq.y, lq.x);
  float a = th - uTunnel.x; a -= TAU * floor (a / TAU + 0.5);
  float L = uTunnel.y - uTunnel.x;
  return (smoothstep (-0.0008, 0.0012, a) - smoothstep (L - 0.0012, L + 0.0008, a)) * (1.0 - smoothstep (uTunnel.z - 0.5, uTunnel.z + 1.0, ad));
}
// the curtains' windows round the turn (where the three hang), for what reflects the sky
float curtainWin (float az, float c, float w) { float d = az - c; d -= floor (d + 0.5); return 1.0 - smoothstep (w * 0.4, w, abs (d)); }
vec3 envSky (vec3 R) {
  float el = max (R.y, 0.0);
  float az = atan (R.x, -R.z) / TAU;
  float win = 1.3 * curtainWin (az, 0.02, 0.30) + 0.9 * curtainWin (az, 0.27, 0.22) + 1.1 * curtainWin (az, 0.52, 0.28) + 0.8 * curtainWin (az, 0.78, 0.25);
  vec3 au = vec3 (0.35, 0.95, 0.70) * smoothstep (0.02, 0.10, el) * exp (-max (el - 0.12, 0.0) / 0.22)
          + vec3 (0.55, 0.40, 0.95) * smoothstep (0.20, 0.45, el) * exp (-max (el - 0.45, 0.0) / 0.4) * 0.35;
  vec3 c = vec3 (0.10, 0.11, 0.14) * exp (-el / 0.25) + vec3 (0.02, 0.022, 0.04) + au * win * 0.5 * (1.0 + 1.2 * uStorm);
  // the ranges round the horizon, black (what makes chrome read as chrome: sky, a dark band, the bright line, the ground)
  float ridge = 0.035 + 0.03 * (0.5 + 0.5 * sin (az * TAU * 3.0 + 1.3)) + 0.012 * sin (az * TAU * 11.0 + 0.4);
  return mix (c, vec3 (0.012, 0.012, 0.016), 1.0 - smoothstep (ridge - 0.004, ridge + 0.004, el));
}
vec3 envFloor (vec3 R) {
  float dn = max (-R.y, 0.0);
  return vec3 (0.010, 0.009, 0.010) + vec3 (1.0, 0.32, 0.07) * (0.08 + 0.35 * uEnvHot) * exp (-dn / 0.12) * (1.0 + uStorm + 1.5 * hitGlow ());
}
// what a mirror sees: the sky, the plain, and between them the bright line of the far plain's crowded cracks
vec3 envAt (vec3 R) {
  vec3 e = R.y > 0.0 ? envSky (R) : envFloor (R);
  return e + vec3 (1.0, 0.62, 0.35) * (0.45 + 0.4 * uEnvHot) * exp (-abs (R.y + 0.008) / 0.012);
}
// the fires near (a pyro column, a dragon's flame): an orange light falling off with distance
vec3 fireLight (vec3 p, vec3 n) {
  vec3 c = vec3 (0.0);
  for (int i = 0; i < 2; ++i) {
    if (uPL[i].w <= 0.0) continue;
    vec3 L = uPL[i].xyz - p; float d2 = dot (L, L);
    c += vec3 (1.0, 0.45, 0.12) * uPL[i].w * (0.3 + 0.7 * max (dot (n, L * inversesqrt (d2 + 1e-3)), 0.0)) / (1.0 + d2 / 120.0);
  }
  return c;
}
// the rover's lamps: a soft cone along the way, out to some 70 m
float lampsAt (vec3 p) {
  vec2 q = p.xz - uLamps.xy; float d = length (q);
  float along = dot (q, uLamps.zw);
  float cone = smoothstep (0.55, 0.9, along / max (d, 1e-3));
  return cone * smoothstep (2.0, 8.0, along) / (1.0 + sq (d / 28.0)) * step (p.y, uCamRel.y + 6.0);
}
// light on a matte surface: the aurora from above, the stars, the flash (all three dimmed by ambK: in the tunnel),
// the fires, the lamps
vec3 lightMatteK (vec3 alb, vec3 n, vec3 p, float ambK) {
  vec3 amb = uAurAmb * (0.45 + 0.55 * n.y) + uStarAmb * (0.6 + 0.4 * n.y) + uFlashCol * uFlash * (0.25 + 0.55 * max (n.y, 0.0));
  return alb * (amb * ambK + fireLight (p, n) + vec3 (0.85, 0.80, 0.70) * 0.55 * lampsAt (p));
}
vec3 lightMatte (vec3 alb, vec3 n, vec3 p) { return lightMatteK (alb, n, p, 1.0); }
// chrome: the world mirrored, a little of it blurred (k: how much of it, 1 polished, less aged)
vec3 steel (vec3 n, vec3 V, vec3 p, float k) {
  vec3 R = reflect (V, n);
  float fres = pow (1.0 - max (-dot (V, n), 0.0), 5.0);
  vec3 col = envAt (R) * mix (0.75, 1.0, fres) * vec3 (0.92, 0.95, 1.0) * k;
  col += uFlashCol * uFlash * (0.30 + 0.70 * max (n.y, 0.0)) * 0.55;
  col += uAurAmb * 0.18 + fireLight (p, n) * 0.9 + vec3 (0.8, 0.8, 0.75) * 0.2 * lampsAt (p);
  col += vec3 (1.0, 0.36, 0.08) * (0.04 + 0.16 * uEnvHot) * (1.0 + uStorm + 1.5 * hitGlow ()) * max (-n.y, 0.0) * k;
  return col;
}
vec3 applyFar (vec3 col, float dist) { return mix (col, uFar, smoothstep (700.0, 1700.0, dist) * 0.9); }
