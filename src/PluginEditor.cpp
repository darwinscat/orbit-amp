#include "PluginEditor.h"

namespace orbitamp
{

AmpEditor::AmpEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p), chrome (p), strip (p), faceplate (p),
      gateStrip (p.gateKeyDb, p.gateMeterDb, p.inClip, *p.apvts.getParameter (params::gateThreshold),
                 *p.apvts.getParameter (params::inTrim), *p.apvts.getParameter (params::gateOn),
                 *p.apvts.getParameter (params::gateDecay)),
      outStrip (p.outDb, p.outClip, *p.apvts.getParameter (params::outTrim)),
      tunerStrip (p.tunerEar), footer (p), demoStrip (p)
{
    addAndMakeVisible (chrome);
    addAndMakeVisible (strip);
    addAndMakeVisible (faceplate);
    addAndMakeVisible (gateStrip);
    addAndMakeVisible (outStrip);
    addAndMakeVisible (tunerStrip);
    addAndMakeVisible (footer);
    addAndMakeVisible (demoStrip);   // TEMPORARY
    addAndMakeVisible (glyphs);      // TEMPORARY
    addChildComponent (setup);       // hidden until the toolbar's gear opens it

    chrome.onShowSetup = [this] { setup.open(); };

    // A thumb opens its link across the panel; the lit thumb closes it again. The strip is the map
    // either way — it never leaves.
    strip.onOpen = [this] (ChainLink link)
    {
        const auto next = faceplate.zoom() == link ? std::nullopt : std::optional<ChainLink> (link);
        faceplate.setZoom (next);
        strip.setActive (next);
    };

    // The tuner strip is the same gesture aimed at the same place: glance below, look above.
    tunerStrip.onClick = [this] { strip.onOpen (ChainLink::tuner); };

    // And the gate's sliver: a clean click opens its zoom; drags belong to the threshold.
    gateStrip.onClick = [this] { strip.onOpen (ChainLink::gate); };

    // And the open lens closes on any click — the tuner has nothing to operate, only to see.
    faceplate.onTunerDismiss = [this] { strip.onOpen (ChainLink::tuner); };

    // Devices came or went while the window was open: the engine re-reads the folder, then the
    // captured blocks rebuild their selectors from the lists that changed under them.
    setup.onDevicesChanged = [this]
    {
        amp.rescanDevices();
        faceplate.deviceChanged();
    };

    // Dragging the corner IS the zoom: the aspect is locked, so width alone determines the factor.
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setResizeLimits (juce::roundToInt (baseWidth  * AmpProcessor::minScale),
                     juce::roundToInt (baseHeight * AmpProcessor::minScale),
                     juce::roundToInt (baseWidth  * AmpProcessor::maxScale),
                     juce::roundToInt (baseHeight * AmpProcessor::maxScale));

    // As large as the screen allows, up to the size the plugin WANTS to open at. Asking for 2x on a
    // display that cannot hold it does not give 2x — it gives whatever the window manager shrinks it
    // to, and reading that back as the next window's wish is how a plugin walks itself down to the
    // minimum over a few launches.
    float s = AmpProcessor::preferredScale;

    if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
    {
        const auto area = display->userArea;
        const float fits = juce::jmin ((float) area.getWidth()  / (float) baseWidth,
                                       (float) (area.getHeight() - titleBarAllowance) / (float) baseHeight);
        s = juce::jlimit (AmpProcessor::minScale, s, fits);
        amp.setEditorScale (s);
    }

    setSize (juce::roundToInt (baseWidth * s), juce::roundToInt (baseHeight * s));
}

void AmpEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::ground);
}

void AmpEditor::resized()
{
    const float s = (float) getWidth() / (float) baseWidth;
    amp.setEditorScale (s);

    // Bounds stay in design units; the transform does all the scaling, margins included.
    const auto zoom = juce::AffineTransform::scale (s);

    chrome.setBounds (margin, margin, FaceplateView::designWidth, Chrome::designHeight);
    chrome.setTransform (zoom);

    // The chain map first: the whole signal path, readable before any block is.
    const int stripY = margin + Chrome::designHeight + chromeGap;
    strip.setBounds (margin, stripY, FaceplateView::designWidth, ChainStrip::designHeight);
    strip.setTransform (zoom);

    // The gate's IN sliver takes the faceplate row's left edge; the faceplate wears the rest.
    const int faceplateY = stripY + ChainStrip::designHeight + chromeGap;
    gateStrip.setBounds (margin, faceplateY, GateStrip::designWidth, FaceplateView::designHeight);
    gateStrip.setTransform (zoom);

    outStrip.setBounds (margin + FaceplateView::designWidth - OutStrip::designWidth, faceplateY,
                        OutStrip::designWidth, FaceplateView::designHeight);
    outStrip.setTransform (zoom);

    faceplate.setBounds (margin + GateStrip::designWidth + chromeGap, faceplateY,
                         FaceplateView::designWidth - GateStrip::designWidth - OutStrip::designWidth
                             - 2 * chromeGap,
                         FaceplateView::designHeight);
    faceplate.setTransform (zoom);

    // The always-on needle, full width, above the footer's facts.
    const int tunerY = faceplateY + FaceplateView::designHeight + chromeGap;
    tunerStrip.setBounds (margin, tunerY, FaceplateView::designWidth, TunerStrip::designHeight);
    tunerStrip.setTransform (zoom);

    const int footerY = tunerY + TunerStrip::designHeight + chromeGap;
    footer.setBounds (margin, footerY, FaceplateView::designWidth, Footer::designHeight);
    footer.setTransform (zoom);

    // TEMPORARY — the audition player and the glyph review strip, under the footer.
    const int demoY = footerY + Footer::designHeight;
    demoStrip.setBounds (margin, demoY, FaceplateView::designWidth, DemoStrip::designHeight);
    demoStrip.setTransform (zoom);

    glyphs.setBounds (margin, demoY + DemoStrip::designHeight,
                      FaceplateView::designWidth, GlyphPreview::designHeight);
    glyphs.setTransform (zoom);

    // The overlay covers the whole editor, margins included, in the same design units.
    setup.setBounds (0, 0, baseWidth, baseHeight);
    setup.setTransform (zoom);
}

} // namespace orbitamp
