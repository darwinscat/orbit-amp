// Where the cabin's panel covers the view (UnderwaterWorld::PanelMask's covered blocks, as rectangles in NDC), laid
// into the scene's depth at its nearest, before the scene: nothing the scene draws there is shaded.
in vec2 aPos;
void main() { gl_Position = vec4 (aPos, -1.0, 1.0); }
