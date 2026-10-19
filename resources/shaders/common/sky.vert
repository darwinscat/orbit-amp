out vec2 vNdc;
void body() {
  vec2 p = vec2 ((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
  vNdc = p; gl_Position = vec4 (p, 1.0, 1.0);
}

// Attribute 0, always 0 (gpu::attributeless): read, so a browser sees the program take an attribute and does not
// emulate one for it — adding it changes no position.
in float aZero;
void main() { body(); gl_Position.x += aZero; }
