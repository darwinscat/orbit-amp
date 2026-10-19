in vec4 aSeg; in vec2 aW;
uniform float uAspect; uniform vec2 uRes;
out float vS; out float vA; out vec2 vN;
void main(){
  vec2 k = vec2(uAspect, 1.0);
  vec2 a = aSeg.xy * k, b = aSeg.zw * k;
  vec2 d = b - a; float L = length(d); vec2 dn = L > 1e-6 ? d / L : vec2(0.0, 1.0); vec2 nn = vec2(-dn.y, dn.x);
  float w = max(aW.x, 0.8 / uRes.y) + 1.0 / uRes.y;
  int cv = gl_VertexID - (gl_VertexID / 6) * 6;
  float along = (cv == 1 || cv == 2 || cv == 4) ? 1.0 : 0.0, side = (cv == 2 || cv == 4 || cv == 5) ? 1.0 : -1.0;
  vec2 q = mix(a - dn * w * 0.5, b + dn * w * 0.5, along) + nn * (side * w);
  gl_Position = vec4((q / k) * 2.0 - 1.0, 0.0, 1.0);
  vS = side; vA = aW.y * min(1.0, aW.x * uRes.y / 0.8); vN = nn / k;
}
