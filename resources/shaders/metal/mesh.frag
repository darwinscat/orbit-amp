// From the prototype's metal-shaders.js (FS_MESH), its body as it is but for the fade's dither: keyed to the whole view's pixel,
// gl_FragCoord + uFragOff (MetalGl.cpp draws the scene into the cabin window's box only).
#include "metal/ftex.glsl"
#include "metal/common.glsl"
#include "metal/light.glsl"

uniform vec4 uMeshCave;          // a cave in the file's frame: half width, from y, under z, strength
uniform float uMeshFade;
uniform float uMeshSteel;        // 1: its painted faces are steel too (the skeletons, made steel)
uniform vec4 uMeshPaint;         // a metallic paint (rgb, on): the faces of that colour get it (candy-apple red, lake-placid blue...)
uniform vec4 uCensorC[2], uCensorS[2];   // a censored headstock's box in the file's frame (centre; half size, on): its pixels
                                         // are marked in the scene's alpha (0.5) for the post pass's mosaic
in vec3 vRel; in vec3 vN; in vec3 vCol; flat in int vKind; in vec3 vLoc;
out vec4 fragColor;
uniform vec2 uFragOff;
void main() {
  if (uMeshFade < 0.999 && fract (52.9829189 * fract (dot ((gl_FragCoord.xy + uFragOff), vec2 (0.06711056, 0.00583715)))) > uMeshFade) discard;
  float dist = length (vRel); vec3 V = vRel / dist;
  vec3 n = normalize (vN);
  if (dot (n, V) > 0.0) n = -n;
  vec3 wp = vRel + uCamRel;
  float cave = 0.0;
  if (uMeshCave.w > 0.0)
    cave = uMeshCave.w * (1.0 - smoothstep (uMeshCave.x - 1.5, uMeshCave.x + 1.0, abs (vLoc.x))) * smoothstep (uMeshCave.y - 2.0, uMeshCave.y + 3.0, vLoc.y) * (1.0 - smoothstep (uMeshCave.z - 2.0, uMeshCave.z + 1.0, vLoc.z));
  vec3 col;
  float k = dot (vCol, vec3 (0.333)) / 0.68;
  // the paint: the faces the file gives the paint's own colour (exported as metal, kind 1, in that colour)
  if (uMeshPaint.w > 0.5 && length (vCol - uMeshPaint.rgb) < 0.05) {
    // metallic paint: the colour's flakes mirror the world tinted by it, a clear coat over them mirrors it as it is
    vec3 R = reflect (V, n), env = envAt (R);
    float fres = pow (1.0 - max (-dot (V, n), 0.0), 5.0);
    col = lightMatte (uMeshPaint.rgb * 0.45, n, wp) + env * uMeshPaint.rgb * 1.6 + env * (0.06 + 0.5 * fres) + fireLight (wp, n) * uMeshPaint.rgb;
  }
  else if (vKind == 1 || (vKind == 0 && uMeshSteel > 0.5)) col = steel (n, V, wp, max (k, 0.8)) * (1.0 - 0.9 * cave);
  else if (vKind == 2) col = envAt (reflect (V, n)) * 0.25;
  else if (vKind == 3) col = mix (vec3 (1.0, 0.24, 0.03), vec3 (1.0, 0.6, 0.2), 0.5 + 0.5 * sin (uTime * 0.9)) * (1.5 + 0.6 * uStorm + 2.0 * hitGlow ());
  else col = lightMatte (vCol, n, wp) * (1.0 - 0.9 * cave);
  col = applyFar (col, dist);
  float mark = 0.0;
  for (int i = 0; i < 2; ++i)
    if (uCensorS[i].w > 0.5 && all (lessThan (abs (vLoc - uCensorC[i].xyz), uCensorS[i].xyz * 1.06))) mark = 0.5;
  fragColor = vec4 (col, mark);
}
