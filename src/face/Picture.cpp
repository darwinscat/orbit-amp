// SPDX-License-Identifier: AGPL-3.0-or-later
// Copyright (c) 2026 Darwin's Cat — Oleh Tsymaienko <oleh@darwinscat.com> & Alisa Lafoks <alisa@darwinscat.com>. Part of OrbitAmp — see LICENSE.

#include "Picture.h"

namespace orbitamp::face
{

Picture::Picture()
{
    setInterceptsMouseClicks (false, false);
}

void Picture::setHistory (bool undo, bool redo, int reg)
{
    if (undo == canUndo && redo == canRedo && reg == activeRegister)
        return;
    canUndo = undo;
    canRedo = redo;
    activeRegister = reg;
    repaint (historyArea());
}

void Picture::setStrips (const Strips& now)
{
    if (now == strips)
        return;
    strips = now;
    layer = {};         // the sockets and the frame are in the layer
    repaint();
}

juce::Image Picture::staticImage (float scale) const
{
    using namespace stage;

    juce::Image img (juce::Image::ARGB, juce::roundToInt (width * scale), juce::roundToInt (height * scale), true);
    juce::Graphics g (img);
    g.addTransform (juce::AffineTransform::scale (scale));
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    paintStatic (g, scale);
    return img;
}

void Picture::paint (juce::Graphics& g)
{
    using namespace stage;

    const float scale = g.getInternalContext().getPhysicalPixelScaleFactor();
    if (! layer.isValid() || std::abs (scale - layerScale) > 1.0e-3f)
    {
        layerScale = scale;
        layer = juce::Image (juce::Image::ARGB, juce::roundToInt (width * scale), juce::roundToInt (height * scale), true);
        juce::Graphics lg (layer);
        lg.addTransform (juce::AffineTransform::scale (scale));
        lg.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        paintStatic (lg, scale);
        paintStrips (lg, scale);
    }
    g.drawImage (layer, juce::Rectangle<float> (0.0f, 0.0f, width, height));
    paintLive (g);
}

} // namespace orbitamp::face
