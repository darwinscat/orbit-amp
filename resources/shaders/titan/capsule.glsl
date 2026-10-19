
out vec2 vLoc; out float vLen; out float vRad; out float vSoft;
float capsule(vec2 a, vec2 b, float rad, int cv, vec2 res){
  vec2 ax = b - a; float len = length(ax);
  vec2 dn = len > 1e-3 ? ax / len : vec2(0.0, 1.0); vec2 nn = vec2(-dn.y, dn.x);
  float ext = rad + 1.0;
  bool head = (cv == 1 || cv == 2 || cv == 4);
  float side = (cv == 2 || cv == 4 || cv == 5) ? 1.0 : -1.0;
  vec2 q = (head ? b + dn * ext : a - dn * ext) + nn * (side * ext);
  gl_Position = vec4(q / res * 2.0 - 1.0, 0.0, 1.0);
  vLoc = vec2(head ? len + ext : -ext, side * ext); vLen = len; vRad = rad;
  return len;
}
