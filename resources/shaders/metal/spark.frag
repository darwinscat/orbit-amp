// From the prototype's metal-shaders.js (FS_SPARK), its body as it is.

in vec3 vCol; in vec2 vUv; in float vA;
out vec4 fragColor;
void main() { float a = vA * (1.0 - smoothstep (0.14, 0.5, length (vUv - 0.5))) * 1.3; fragColor = vec4 (vCol * a, 0.0); }
