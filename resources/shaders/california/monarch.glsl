
// w = (out, along): out 0 at the body .. 1 at the forewing's tip, along + toward the head. under: 0 the upper side,
// 1 the underside (paler); detail: 1 near, 0 far (one blended colour where the veins and dots would shimmer).
// Returns the colour and the coverage, soft over 'soft' at the edge.
// The wings' colours: a monarch's orange and seven neon ones (the owner: hot pink, lime, electric cyan, violet, lemon,
// tangerine, magenta); the veins and the dots stay.
vec3 wingColour (int i) {
  return i == 0 ? vec3 (1.0, 0.46, 0.05) : i == 1 ? vec3 (1.0, 0.16, 0.60) : i == 2 ? vec3 (0.55, 1.0, 0.10) : i == 3 ? vec3 (0.05, 0.88, 1.0)
       : i == 4 ? vec3 (0.60, 0.22, 1.0) : i == 5 ? vec3 (1.0, 0.93, 0.08) : i == 6 ? vec3 (1.0, 0.60, 0.0) : vec3 (0.96, 0.10, 0.92);
}
vec4 monarch (vec2 w, float under, float detail, float soft, vec3 base) {
  float r = length (w), th = atan (w.y, max (w.x, 1.0e-4));
  float tf = clamp ((th - 0.02) / 1.36, 0.0, 1.0), tb = clamp ((th + 1.45) / 1.57, 0.0, 1.0);
  float Rf = (th > 0.02 && th < 1.38) ? 0.40 + 0.60 * pow (sin (3.14159265 * tf), 0.55) : 0.0;
  float Rh = (th > -1.45 && th < 0.12) ? 0.30 + 0.42 * pow (sin (3.14159265 * tb), 0.70) : 0.0;
  bool fore = Rf > Rh;
  float R = max (Rf, Rh);
  float inside = R - r;
  float cover = smoothstep (0.0, soft, inside);
  if (cover <= 0.0) return vec4 (0.0);
  vec3 orange = under > 0.5 ? mix (base, vec3 (1.0, 0.96, 0.88), 0.30) : base;
  vec3 ink = vec3 (0.05, 0.035, 0.03), white = vec3 (0.97, 0.95, 0.90);
  vec3 c = orange;
  float edge = 0.09;
  if (inside < edge) {
    // the black border, with white dots in two rows
    c = ink;
    float per = fore ? 15.0 : 11.0;
    float along = (fract (th * per) - 0.5) * R / per;
    float d1 = length (vec2 (along, inside - 0.050));
    float along2 = (fract (th * per * 2.0) - 0.5) * R / per * 0.5;
    float d2 = length (vec2 (along2, inside - 0.018));
    c = mix (c, white, max (1.0 - smoothstep (0.010, 0.017, d1), 1.0 - smoothstep (0.006, 0.010, d2)));
  } else {
    // the veins, black, radiating from the root and thicker near it; a cross-vein; the forewing's black apex
    float vk = fore ? 5.6 : 4.4;
    float v = abs (fract (th * vk + 0.25) - 0.5) / vk * r;
    float vw = 0.004 + 0.009 * (1.0 - r);
    float vein = 1.0 - smoothstep (vw, vw + 0.005, v);
    float cross_ = 1.0 - smoothstep (0.004, 0.009, abs (r - R * (fore ? 0.56 : 0.62)));
    c = mix (c, ink, max (vein, cross_ * 0.8));
    if (fore && r > 0.76 * R && th > 0.38 && th < 0.95) {
      float sp = length (vec2 ((fract (th * 9.0) - 0.5) * 0.10, r - 0.79 * R));
      c = mix (ink, mix (base, vec3 (1.0), 0.45), 1.0 - smoothstep (0.020, 0.030, sp));
    }
  }
  if (r < 0.08) c = ink;
  vec3 mean = mix (orange, ink, 0.22);
  return vec4 (mix (mean, c, detail), cover);
}
