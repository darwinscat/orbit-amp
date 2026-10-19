
uniform vec2  uOrigin;
uniform vec2  uSize;
uniform float uTravel;
uniform float uSpeed;
uniform float uWarp;
uniform float uFocus;
uniform float uRoll;
uniform float uSlide;
uniform vec4  uDrift;
uniform vec2  uCloudDrift;

float hash (vec2 p)
{
    p = fract (p * vec2 (123.34, 456.21));
    p += dot (p, p + 45.32);
    return fract (p.x * p.y);
}

float noise (vec2 p)
{
    vec2 i = floor (p), f = fract (p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix (mix (hash (i), hash (i + vec2 (1.0, 0.0)), u.x),
                mix (hash (i + vec2 (0.0, 1.0)), hash (i + vec2 (1.0, 1.0)), u.x), u.y);
}

float fbm (vec2 p)
{
    float s = 0.0, a = 0.5;
    for (int i = 0; i < 5; ++i)
    {
        s += a * noise (p);
        p = p * 2.03 + vec2 (1.7, 9.2);
        a *= 0.5;
    }
    return s;
}

void main()
{
    // Measured from the point the stars stream out of, in the windshield's heights; and the sky's own
    // frame turned against the bank about that point.
    vec2 uv = (gl_FragCoord.xy - uOrigin - 0.5 * uSize) / uSize.y - vec2 (uFocus, 0.0);
    mat2 bank = mat2 (cos (uRoll), -sin (uRoll), sin (uRoll), cos (uRoll));
    float r = length (uv);
    vec3 col = mix (vec3 (0.028, 0.024, 0.086), vec3 (0.004, 0.004, 0.016), clamp (r * 0.8, 0.0, 1.0));

    // The clouds: each swells from far to past the glass while it brightens and fades, two half a
    // cycle apart, so one is always arriving as the other leaves.
    for (int i = 0; i < 2; ++i)
    {
        float ph = fract (uTravel * 0.12 + float (i) * 0.5);
        float zoom = mix (2.6, 0.35, ph * ph);
        vec2 p = bank * (uv * zoom + vec2 (uCloudDrift[i], 0.0)) + vec2 (float (i) * 7.3 + 3.0, float (i) * 3.1 + 1.0);
        vec2 q = vec2 (fbm (p * 1.1), fbm (p * 1.1 + vec2 (5.2, 1.3)));
        float d = fbm (p * 1.4 + 3.2 * q);
        vec3 c = mix (vec3 (0.16, 0.07, 0.38), vec3 (0.42, 0.17, 0.72), smoothstep (0.45, 0.72, d));
        c = mix (c, vec3 (0.69, 0.29, 0.66), smoothstep (0.68, 0.9, d));
        c += vec3 (1.0, 0.54, 0.24) * smoothstep (0.80, 0.95, fbm (p * 2.6 + 9.0)) * 0.45;
        col += c * smoothstep (0.42, 0.80, d) * sin (ph * 3.14159) * 0.8;
    }

    // At speed the light ahead gathers into a glow.
    col += vec3 (0.81, 0.77, 1.0) * uWarp * 0.32 * exp (-r * r * 5.0);

    // The stars. A streak runs back along the ray, toward the point they stream out of, and — while a
    // turn sweeps them — sideways, back where the sweep brought it from: both in the layers' own frame.
    vec2 dir = uv / max (r, 1e-4);
    vec2 back = bank * -dir;
    vec2 aside = bank * vec2 (1.0, 0.0);
    for (int l = 0; l < 4; ++l)
    {
        float z = fract (float (l) * 0.25 + uTravel * 0.45);
        float fade = smoothstep (0.0, 0.3, z) * smoothstep (1.0, 0.86, z);
        float scale = mix (16.0, 1.2, z);
        vec2 p = bank * (uv * scale + vec2 (uDrift[l], 0.0)) + vec2 (float (l) * 17.1, float (l) * 31.7);
        vec2 home = floor (p);
        float len = 0.004 + uSpeed * 0.18 * z * z;
        float size = 0.010 + 0.035 * z * z;
        vec2 trail = back * 2.0 * len + aside * uSlide * 0.035;
        float reach = dot (trail, trail);

        // A star sits well inside its cell, but its streak runs back for the trail's length — at speed,
        // out of the cell it started in. So the cells round this one are asked as well, or the streak
        // would end at the cell's edge as if cut. A slow ship's streaks never leave home, and then only
        // home is asked.
        bool roams = sqrt (reach) + size > 0.15;
        for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i)
        {
            if (! roams && (i != 0 || j != 0)) continue;
            vec2 cell = home + vec2 (float (i), float (j));
            float h = hash (cell);
            if (h < 0.62) continue;
            vec2 at = p - cell - 0.5 - (vec2 (hash (cell + 3.1), hash (cell + 7.7)) - 0.5) * 0.7;
            float d = length (at - trail * clamp (dot (at, trail) / max (reach, 1e-8), 0.0, 1.0));
            float star = smoothstep (size, 0.0, d) * fade * (0.55 + 0.45 * fract (h * 13.0));
            vec3 tint = fract (h * 7.0) < 0.12 ? vec3 (0.72, 0.61, 1.0)
                      : fract (h * 7.0) < 0.18 ? vec3 (1.0, 0.78, 0.6) : vec3 (0.91, 0.93, 1.0);
            col += tint * star * 1.4;
        }
    }

    gl_FragColor = vec4 (col, 1.0);
}
