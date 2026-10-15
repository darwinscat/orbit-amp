// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#pragma once

#include "Mars1993.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orbitamp::redpaw
{

/** A DRIVE THE WINDSCREEN HOSTS: a world and its GPU renderer behind one face, so the same glass can
    look out on the Plains of Mars or across the ice of a moon. The windscreen hands the drive its gear,
    TURBO, PARK and the steering, steps it once a frame and has it draw; what the world is — its height
    function, its sky, what flies at the glass — is the drive's own. A product passes the drive it wants,
    made for its own number of gears, to the windscreen's constructor.

    The world's half is called from the thread that drives (the GL thread while the GPU draws); the
    GPU's half is the GL thread's only, between create() and destroy(), as PlainsGl is. */
class Drive
{
public:
    virtual ~Drive() = default;

    /** What it is, for a status line: "Plains", "Ice". */
    virtual const char* name() const noexcept = 0;

    /** How many gears it drives in: the product's, handed to the drive as it is made. */
    virtual int numGears() const noexcept = 0;

    /** The drive's inputs. */
    virtual void setGear (int gear) = 0;            // 1..numGears()
    virtual void setTurbo (bool on) = 0;
    virtual void setParked (bool on) = 0;
    virtual void setSteer (float amount) = 0;       // -1..1, held

    /** One step of the drive. */
    virtual void step (double dt) = 0;

    /** Placed where it stands with no history: the speed and the air the gear asks for reached, the
        camera evaluated directly. For a still. */
    virtual void settle (int gear, bool turbo, bool parked) = 0;

    /** Put somewhere: absolute world metres and a heading in radians, 0 = -z. The world made first if
        it was not yet (tens of milliseconds, once). */
    virtual void placeAt (double x, double z, double heading) = 0;

    /** The world's clock, where it has one: the seconds the sky moves by, the metres driven that what
        flies at the glass cycles by. A drive without one ignores it. */
    virtual void setClock (double /*seconds*/, double /*metresDriven*/) {}

    /** The speed as driven, m/s, eased. */
    virtual float speed() const noexcept = 0;
    /** The speed the gear and TURBO ask for, m/s. */
    virtual float speedFor (int gear, bool turbo) const noexcept = 0;

    /** The 1993 landscape's colours for this world. */
    virtual Mars1993::Look look1993() const noexcept = 0;

    /** What stands within `metres` of the rover, one a line — "kind x z dx dz distance" — for a shot
        aimed at it. A world with nothing standing in it says nothing. */
    virtual std::string landmarks (double /*metres*/) const { return {}; }

    //==========================================================================
    /** The GPU's half — see PlainsGl. create() is false when a shader would not build here. */
    virtual bool create() = 0;
    virtual void destroy() = 0;
    /** The scene at (w * scale) x (h * scale), blitted to `targetFrameBuffer` over (x, y, w, h) — GL's
        own pixels, from its bottom-left corner. */
    virtual void render (int w, int h, float scale, uint32_t targetFrameBuffer, int x, int y) = 0;
    /** What the last frames cost on the GPU, milliseconds, a running mean; -1 until known. */
    virtual float gpuMillis() const noexcept = 0;
    /** The offscreen frame as RGBA bytes, rows bottom-up as GL has them. */
    virtual bool readFrame (std::vector<uint8_t>& rgba, int& w, int& h) = 0;
    virtual int frameWidth() const noexcept = 0;
    virtual int frameHeight() const noexcept = 0;
    /** The scene drawn this many times a frame — for measuring: the timer then reads that many scenes,
        clear of the frame's fixed cost. 1 is the picture. A drive may ignore it. */
    virtual void setRepeats (int /*times*/) {}
};

} // namespace orbitamp::redpaw
