// From the prototype's metal-shaders.js (VS_RIBBON), its body as it is.
#include "metal/vtex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

in vec4 aA;      // a (rel), its half width (m)
in vec4 aB;      // b (rel), its half width (m)
in vec4 aC;      // colour, strength
in vec4 aK;      // kind, the along-coordinate at a and at b, -
uniform mat4 uView, uProj; uniform vec2 uRes;
out float vAcross; out float vAlong; out vec4 vColour; out float vKind; out float vCov;
void main() {
  int k = gl_VertexID;
  float t = (k == 1 || k == 2 || k == 4) ? 1.0 : 0.0, sd = (k == 2 || k == 4 || k == 5) ? 1.0 : -1.0;
  vec4 va = uView * vec4 (aA.xyz, 1.0), vb = uView * vec4 (aB.xyz, 1.0);
  const float zn = -0.6;
  if (va.z > zn && vb.z > zn) { gl_Position = vec4 (0.0, 0.0, 2.0, 1.0); vColour = vec4 (0.0); vAcross = 0.0; vAlong = 0.0; vKind = 0.0; vCov = 0.0; return; }
  float wa = aA.w, wb = aB.w, ta = aK.y, tb = aK.z;
  if (va.z > zn) { float f = (zn - vb.z) / (va.z - vb.z); va = mix (vb, va, f); wa = mix (wb, wa, f); ta = mix (tb, ta, f); }
  if (vb.z > zn) { float f = (zn - va.z) / (vb.z - va.z); vb = mix (va, vb, f); wb = mix (wa, wb, f); tb = mix (ta, tb, f); }
  vec4 pa = uProj * va, pb = uProj * vb;
  vec2 na = pa.xy / pa.w, nb = pb.xy / pb.w;
  vec2 hres = uRes * 0.5;
  vec2 dpx = (nb - na) * hres; float L = length (dpx);
  vec2 dir = L > 1e-4 ? dpx / L : vec2 (1.0, 0.0);
  vec2 perp = vec2 (-dir.y, dir.x);
  float pxa = wa * uProj[0][0] * hres.x / -va.z, pxb = wb * uProj[0][0] * hres.x / -vb.z;
  float px = mix (pxa, pxb, t), minPx = aK.x < 0.5 ? 1.6 : 2.0;
  float wpx = max (px, minPx);
  vCov = px / wpx;
  vec4 p = mix (pa, pb, t);
  vec2 ndc = p.xy / p.w + perp * sd * wpx / hres;
  // a little past the ends, so the joints of a jagged line have no gaps
  ndc += dir * (t * 2.0 - 1.0) * wpx * 0.7 / hres;
  gl_Position = vec4 (ndc * p.w, p.z, p.w);
  vAcross = sd; vAlong = mix (ta, tb, t); vColour = aC; vKind = aK.x;
}
