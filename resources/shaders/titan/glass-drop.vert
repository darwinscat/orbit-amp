in vec4 aDrop; in float aDropA;
uniform vec2 uRes;
out vec2 vQ; out vec2 vC; out float vR; out float vA;
void main(){
  int cv = gl_VertexID - (gl_VertexID / 6) * 6;
  vec2 c = vec2((cv == 1 || cv == 2 || cv == 4) ? 1.0 : -1.0, (cv == 2 || cv == 4 || cv == 5) ? 1.0 : -1.0);
  vec2 rpx = aDrop.zw * uRes;
  float m = 1.0 + 1.5 / max(min(rpx.x, rpx.y), 1.0);
  vQ = c * m; vC = aDrop.xy; vR = min(rpx.x, rpx.y); vA = aDropA;
  gl_Position = vec4((aDrop.xy + c * aDrop.zw * m) * 2.0 - 1.0, 0.0, 1.0);
}
