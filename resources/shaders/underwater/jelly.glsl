
in vec3 aPos; in float aScale; in vec3 aCol; in float aPhase; in vec3 aAxis; in float aAlpha; in vec3 aGlow; in float aTend;
uniform mat4 uViewProj;
// The bell in its own frame, y up the axis: radius and height for u (angle) and v (0 apex .. 1 rim), at contraction c.
vec3 bell (float u, float v, float c) {
  float r = pow (sin (v * 1.5707963), 0.72);
  float y = cos (v * 1.5707963) * 0.80;
  r *= 1.0 - 0.32 * c * v * v;
  y *= 1.0 + 0.26 * c;
  r *= 1.0 + 0.05 * sin (u * 8.0) * v * v;
  y -= 0.05 * v * v * v * cos (u * 8.0);
  return vec3 (r * cos (u), y, r * sin (u));
}
mat3 bellFrame (vec3 up) {
  vec3 helper = abs (up.z) < 0.9 ? vec3 (0.0, 0.0, 1.0) : vec3 (1.0, 0.0, 0.0);
  vec3 right = normalize (cross (up, helper));
  vec3 fwd = cross (right, up);
  return mat3 (right, up, fwd);
}
float contraction (float ph) { float s = 0.5 + 0.5 * sin (ph); return s * s * s; }
