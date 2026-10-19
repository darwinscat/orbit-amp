uniform vec3 uCamR, uCamU, uCamF, uHR;
uniform vec2 uTan, uRes; uniform float uShear, uCamY, uTime;
uniform float uPlay;   // how far the guitar lights the lamps between their blinks: 0 in silence
uniform vec4 uBuoy[8]; uniform int uBuoyN;
out vec2 vUv; flat out int vPart; flat out int vType; out vec3 vCol; out float vA; out float vDist;
const float LAMP_H = 1.95;
vec3 toView(vec3 P){ return vec3(dot(P, uCamR), dot(P, uCamU), dot(P, uCamF)); }
vec2 toPx(vec3 v){ return (vec2(v.x / (v.z * uTan.x), v.y / (v.z * uTan.y) + uShear) * 0.5 + 0.5) * uRes; }
void body(){
  int q = gl_VertexID / 6, cv = gl_VertexID - q * 6;
  int bi = q / 3, part = q - bi * 3;
  vPart = part; vType = 0; vUv = vec2(0.0); vCol = vec3(0.0); vA = 0.0; vDist = 0.0;
  gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
  if (bi >= uBuoyN) return;
  vec4 Bu = uBuoy[bi];
  vType = int(Bu.z + 0.5);
  float bob = 0.07 * sin(uTime * 1.3 + Bu.w * 6.2832);
  vec3 base = vec3(Bu.x, -uCamY + bob, Bu.y);
  vec3 vb = toView(base);
  if (vb.z < 2.0) return;
  vDist = length(base);
  vec2 c = vec2((cv == 1 || cv == 2 || cv == 4) ? 1.0 : -1.0, (cv == 2 || cv == 4 || cv == 5) ? 1.0 : 0.0);
  float focal = uRes.y / (2.0 * uTan.y);
  float per = 2.2 + 1.2 * fract(Bu.w * 7.3), ph = mod(uTime + Bu.w * 9.0, per);
  float blink = smoothstep(0.0, 0.05, ph) * (1.0 - smoothstep(0.3, 0.5, ph));
  blink += (1.0 - blink) * uPlay;
  vec3 lampC = vType == 0 ? vec3(1.0, 0.12, 0.07) : (vType == 1 ? vec3(0.16, 1.0, 0.42) : vec3(1.0, 0.62, 0.12));
  float haze = exp(-vDist / 9000.0);
  vCol = lampC * haze;
  if (part == 0) {                                    // the body: a camera-facing card, shape drawn in the FS
    vec3 P = base + uHR * (c.x * 0.6) + vec3(0.0, -0.05 + c.y * 2.2, 0.0);
    vec2 s = toPx(toView(P));
    vec2 sc = toPx(vb);
    float minW = 1.6;                                 // keep far buoys at least a hair wide
    s.x = sc.x + sign(s.x - sc.x) * max(abs(s.x - sc.x), minW);
    gl_Position = vec4(s / uRes * 2.0 - 1.0, 0.0, 1.0);
    vUv = c; vA = blink;
  } else if (part == 1) {                             // the lamp's glow, never smaller than a few pixels
    vec2 s = toPx(toView(base + vec3(0.0, LAMP_H, 0.0)));
    float r = max(1.4 * focal / vb.z, 6.0) * (0.55 + 0.9 * blink) + 2.0;
    gl_Position = vec4((s + vec2(c.x, c.y * 2.0 - 1.0) * r) / uRes * 2.0 - 1.0, 0.0, 1.0);
    vUv = vec2(c.x, c.y * 2.0 - 1.0); vA = blink;
  } else {                                            // its reflection, smeared down the ripples toward us
    vec2 w = toPx(vb);
    vec2 m = toPx(toView(vec3(Bu.x, -uCamY - LAMP_H, Bu.y)));
    float spread = 0.06 / (2.0 * uTan.y / uRes.y);
    float r = max(0.8 * focal / vb.z, 3.0) * (0.6 + 0.6 * blink);
    float y0 = m.y - spread, y1 = w.y;
    float yy = mix(y0, y1, c.y);
    gl_Position = vec4(vec2(m.x + c.x * r, yy) / uRes * 2.0 - 1.0, 0.0, 1.0);
    vUv = vec2(c.x, (yy - m.y) / spread); vA = blink;
  }
}

// Attribute 0, always 0 (gpu::attributeless): read, so a browser sees the program take an attribute and does not
// emulate one for it — adding it changes no position.
in float aZero;
void main() { body(); gl_Position.x += aZero; }
