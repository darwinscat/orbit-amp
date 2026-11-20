
uniform vec4 uPendBox;      // the sprite's box on the stage, px: x0, y0, x1, y1
uniform vec2 uPendPivot;    // its pivot, stage px
uniform vec2 uStage;        // the stage, px (1672 x 941)
uniform float uPendAngle;   // the sideways swing (rad, + its bob toward the right)
uniform float uPendFore;    // the fore-aft swing's foreshortening (1 none)
out vec2 vUV;
void main() {
  int i = gl_VertexID;
  vec2 q = vec2 ((i == 1 || i == 2 || i == 4) ? 1.0 : 0.0, (i == 2 || i == 4 || i == 5) ? 1.0 : 0.0);
  vec2 p = mix (uPendBox.xy, uPendBox.zw, q) - uPendPivot;   // y down
  p.y *= uPendFore;
  float c = cos (uPendAngle), s = sin (uPendAngle);
  p = vec2 (c * p.x + s * p.y, c * p.y - s * p.x) + uPendPivot;        // y down: + swings the bob to the right
  gl_Position = vec4 (p.x / uStage.x * 2.0 - 1.0, 1.0 - p.y / uStage.y * 2.0, 0.0, 1.0);
  vUV = q;
}
