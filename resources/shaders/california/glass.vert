
in vec4 aG0;     // the centre (NDC), the half span (canvas px), the turn (rad)
in vec4 aG1;     // the wings' opening (cos: 1 flat open .. 0.08 closed), the alpha, the light, -
uniform vec2 uCanvas;
out vec2 vQ; out float vOpen; out float vA; out float vLight; flat out int vColour;
void main() {
  vColour = int (aG1.w + 0.5);
  int i = gl_VertexID;
  vec2 q = vec2 ((i == 1 || i == 2 || i == 4) ? 1.0 : -1.0, (i == 2 || i == 4 || i == 5) ? 1.0 : -1.0);
  float c = cos (aG0.w), s = sin (aG0.w);
  vec2 px = vec2 (c * q.x - s * q.y, s * q.x + c * q.y) * aG0.z;
  gl_Position = aG1.y < 0.003 ? vec4 (0.0, 0.0, 2.0, 1.0) : vec4 (aG0.xy + px * 2.0 / uCanvas, 0.0, 1.0);
  vQ = q; vOpen = aG1.x; vA = aG1.y; vLight = aG1.z;
}
