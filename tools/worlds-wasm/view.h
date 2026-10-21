// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.
//
// What every world's module has the same: the WebGL 2 context it draws in, and the scene's frame read back for a
// bench. A world's file (plains.cpp, ice.cpp, titan.cpp, sky.cpp) includes this once and exports the calls view.js makes,
// under the same names in each: view_make, view_build, view_context, view_create, view_error, view_gear,
// view_turbo, view_settle, view_frame, view_draw, view_test, view_read, view_read_width,
// view_read_height, view_destroy — and whatever more its world has (titan.cpp: view_wipers, view_lights; sky.cpp:
// view_throttle, view_place).

#pragma once

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <cstdint>
#include <vector>

// The context, on the canvas the JS gave the module as Module.viewCanvas: an OffscreenCanvas in a worker, else
// the page's own. WebGL 2, no depth, stencil or alpha of its own: the scene draws into a frame of its own and is
// blitted. `keep` holds the canvas's pixels for reading back, as a bench wants. No extension is turned on but the
// ones the worlds use — anisotropic filtering here (Titan's ripples), OES_texture_float_linear where Gpu.h asks for
// it — so a browser has nothing to warn about that the worlds never touch.
EM_JS (int, view_open_context, (int keep), {
    return GL.createContext (Module['viewCanvas'], {
        majorVersion: 2, minorVersion: 0, antialias: false, alpha: false, depth: false, stencil: false,
        premultipliedAlpha: true, preserveDrawingBuffer: !! keep, powerPreference: 'default', enableExtensionsByDefault: false });
});

namespace worlds
{
    inline EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = 0;
    inline std::vector<std::uint8_t> frame;
    inline int frameW = 0, frameH = 0;

    /** The context, made current; false when the browser has none to give. */
    inline bool open (int keep)
    {
        context = (EMSCRIPTEN_WEBGL_CONTEXT_HANDLE) view_open_context (keep);
        if (context == 0 || emscripten_webgl_make_context_current (context) != EMSCRIPTEN_RESULT_SUCCESS)
            return false;
        emscripten_webgl_enable_extension (context, "EXT_texture_filter_anisotropic");
        return true;
    }

    /** The scene's last frame as RGBA, rows bottom-up as GL has them, from a renderer's readFrame. */
    template <typename Renderer>
    const std::uint8_t* read (Renderer* view)
    {
        if (view == nullptr || ! view->readFrame (frame, frameW, frameH))
            frameW = frameH = 0;
        return frame.data();
    }

    /** The renderer's GPU objects released while its context is current, then the context. */
    template <typename Renderer>
    void close (Renderer* view)
    {
        if (view != nullptr && context != 0 && emscripten_webgl_make_context_current (context) == EMSCRIPTEN_RESULT_SUCCESS)
            view->destroy();
        if (context != 0)
            emscripten_webgl_destroy_context (context);
        context = 0;
    }
}
