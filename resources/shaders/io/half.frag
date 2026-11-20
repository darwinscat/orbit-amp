// The plumes' bodies shaded at half the scene's pixels (IoGl.cpp, at the ladder's foot), laid over the scene: premultiplied,
// each scene pixel reading the half-size frame where its centre falls.
uniform sampler2D uHalf;
in vec2 vNdc;
out vec4 fragColor;
void main() { fragColor = texture (uHalf, gl_FragCoord.xy / (2.0 * vec2 (textureSize (uHalf, 0)))); }
