// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#if defined (__EMSCRIPTEN__)
 #include <GLES3/gl3.h>
 #include <GLES2/gl2ext.h>      // the extensions' names: anisotropic filtering's, as the desktop's GL has them
 #include <emscripten/html5.h>
 #include <cstdio>
#else
 #include <juce_opengl/juce_opengl.h>
#endif

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace orbitamp::gpu
{

/** THE GL THE FACES DRAW WITH, wherever they are drawn. In the app it is the 3.2 core context JUCE makes on
    every platform, the entry points as juce::gl loads them; on the site it is the same C++ in WebAssembly over
    a WebGL 2 context — OpenGL ES 3.0, each call handed to the browser's by emscripten. A renderer written
    against this reads the same either way: `using namespace orbitamp::gpu;` puts GL in reach, and the few
    things that are not the same in the two are asked of the functions below. Nothing here draws. */

#if defined (__EMSCRIPTEN__)
inline constexpr bool web = true;
#else
using namespace ::juce::gl;
inline constexpr bool web = false;
#endif

/** What every shader's source starts with: GLSL 1.50 in the app, GLSL ES 3.00 at high precision on the web.
    The files carry no #version of their own (src/shaders/Shaders.h); the bodies are the same text in both. */
inline std::string header()
{
    return web ? "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
               : "#version 150\n";
}

/** What goes in front of a shader written in GLSL 1.10's words — `attribute`, `varying`, `gl_FragColor`, no
    #version — for the context current on this thread to read it (shaders::vertexToV3, fragmentToV3). In the app,
    JUCE's answer, as its OpenGLHelpers::translate…ToV3 give it: nothing where the context is older than 3.2 and
    reads 1.10 as it is — a Mac's legacy profile, what a JUCE context is unless it asks for 3.2 — else its
    #version line. On the web, header(): WebGL 2 reads GLSL ES 3.00 only. */
inline std::string legacyHeader()
{
   #if defined (__EMSCRIPTEN__)
    return header();
   #else
    const auto version = juce::OpenGLHelpers::getGLSLVersionString();
    return version == "#version 110" ? std::string() : (version + "\n").toStdString();
   #endif
}

/** True when a context is current on this thread, as a renderer's create() needs one. */
inline bool current() noexcept
{
   #if defined (__EMSCRIPTEN__)
    return emscripten_webgl_get_current_context() != 0;
   #else
    return juce::OpenGLContext::getCurrentContext() != nullptr;
   #endif
}

/** The context current on this thread in its driver's own words — renderer / vendor / version — or nothing
    when none is: what a line about this GPU names it by (src/shaders/GpuLog.h). */
inline std::string driver()
{
    if (! current())
        return {};
    const auto said = [] (GLenum name)
    {
        const auto* text = reinterpret_cast<const char*> (glGetString (name));
        return text != nullptr ? std::string (text) : std::string ("?");
    };
    return said (GL_RENDERER) + " / " + said (GL_VENDOR) + " / " + said (GL_VERSION);
}

/** A VERTEX ARRAY FOR DRAWS THAT TAKE NO ATTRIBUTES — full-screen passes, particles made from the vertex's index —
    but one: aZero, attribute 0, a zero for each of up to attributelessVertices vertices in `buffer`, which such a
    vertex shader adds to its position. Core GL wants a vertex array bound for any draw, and a browser on desktop GL
    emulates attribute 0 for a program that takes no attribute at all (Firefox on the Mac says so in its console).
    The caller deletes both. */
inline constexpr int attributelessVertices = 1 << 16;
inline GLuint attributeless (GLuint& buffer)
{
    GLuint vao = 0;
    glGenVertexArrays (1, &vao);
    glBindVertexArray (vao);
    const std::vector<GLfloat> zeros ((size_t) attributelessVertices, 0.0f);
    glGenBuffers (1, &buffer);
    glBindBuffer (GL_ARRAY_BUFFER, buffer);
    glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (zeros.size() * sizeof (GLfloat)), zeros.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray (0);
    glVertexAttribPointer (0, 1, GL_FLOAT, GL_FALSE, 0, nullptr);
    glBindVertexArray (0);
    glBindBuffer (GL_ARRAY_BUFFER, 0);
    return vao;
}

/** A program out of a vertex and a fragment shader's source: its id, or 0 when either would not compile or
    the two would not link — the driver's words in `log`. The shaders go with the program. */
inline GLuint program (const std::string& vertex, const std::string& fragment, std::string& log)
{
    const GLuint id = glCreateProgram();
    const auto stage = [&] (GLenum type, const std::string& source)
    {
        const GLuint s = glCreateShader (type);
        const GLchar* text = source.c_str();
        const GLint length = (GLint) source.size();
        glShaderSource (s, 1, &text, &length);
        glCompileShader (s);
        GLint ok = GL_FALSE;
        glGetShaderiv (s, GL_COMPILE_STATUS, &ok);
        if (ok == GL_FALSE)
        {
            GLchar words[2048] {};
            GLsizei n = 0;
            glGetShaderInfoLog (s, (GLsizei) sizeof (words), &n, words);
            log = std::string (type == GL_VERTEX_SHADER ? "vertex: " : "fragment: ") + std::string (words, (size_t) n);
            glDeleteShader (s);
            return false;
        }
        glAttachShader (id, s);
        glDeleteShader (s);
        return true;
    };
    GLint linked = GL_FALSE;
    if (stage (GL_VERTEX_SHADER, vertex) && stage (GL_FRAGMENT_SHADER, fragment))
    {
        // A mesh's positions at location 0, where it has them, or the zero an attribute-less pass reads
        // (attributeless): a draw whose attribute 0 is no array a browser has to emulate on desktop GL (Firefox
        // says so), and an instanced one might otherwise get its cells there.
        glBindAttribLocation (id, 0, "aPos");
        glBindAttribLocation (id, 0, "aZero");
        glLinkProgram (id);
        glGetProgramiv (id, GL_LINK_STATUS, &linked);
        if (linked == GL_FALSE)
        {
            GLchar words[2048] {};
            GLsizei n = 0;
            glGetProgramInfoLog (id, (GLsizei) sizeof (words), &n, words);
            log = "link: " + std::string (words, (size_t) n);
        }
    }
    if (linked == GL_FALSE)
    {
        glDeleteProgram (id);
        return 0;
    }
    return id;
}

/** Whether an entry point is there: JUCE loads each one from the driver, null where it has none; on the web
    each is a function of the browser's and always there. */
template <typename Function>
bool loaded (Function* entry) noexcept { return entry != nullptr; }

/** A line for whoever is looking: the debugger's output in the app, the console on the web. */
inline void note ([[maybe_unused]] const std::string& text)
{
   #if defined (__EMSCRIPTEN__)
    std::fprintf (stderr, "%s\n", text.c_str());
   #else
    DBG (juce::String (text));
   #endif
}

/** GPU timer queries: GL 3.3's where the driver loads them. None on the web — WebGL 2's extension for them is
    missing from most browsers, its clock coarsened where it is there — and a renderer that times itself by
    finishing a frame would stall the page for it, so on the web nothing is timed. */
inline bool timerQueries() noexcept
{
   #if defined (__EMSCRIPTEN__)
    return false;
   #else
    return glGenQueries != nullptr && glGetQueryObjectui64v != nullptr;
   #endif
}

inline void beginTimer ([[maybe_unused]] GLuint query)
{
   #if ! defined (__EMSCRIPTEN__)
    glBeginQuery (GL_TIME_ELAPSED, query);
   #endif
}

inline void endTimer()
{
   #if ! defined (__EMSCRIPTEN__)
    glEndQuery (GL_TIME_ELAPSED);
   #endif
}

/** A finished timer query's time, nanoseconds. */
inline std::uint64_t timerNanoseconds ([[maybe_unused]] GLuint query)
{
    std::uint64_t ns = 0;
   #if ! defined (__EMSCRIPTEN__)
    GLuint64 t = 0;
    glGetQueryObjectui64v (query, GL_QUERY_RESULT, &t);
    ns = (std::uint64_t) t;
   #endif
    return ns;
}

/** The format a float texture that is filtered is made in: RGBA32F where 32-bit floats filter linearly —
    always in the app, on the web where the browser has OES_texture_float_linear — else RGBA16F, which
    always does: half floats, steps of a few centimetres on a mountain's height. Uploaded from floats either way. */
inline GLenum filteredFloatFormat() noexcept
{
   #if defined (__EMSCRIPTEN__)
    return emscripten_webgl_enable_extension (emscripten_webgl_get_current_context(), "OES_texture_float_linear") ? GL_RGBA32F : GL_RGBA16F;
   #else
    return GL_RGBA32F;
   #endif
}

/** A monotonic clock, milliseconds: what a frame timed by finishing it reads. */
inline double milliseconds() noexcept
{
    return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace orbitamp::gpu
