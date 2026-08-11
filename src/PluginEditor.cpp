#include "PluginEditor.h"

namespace orbitamp
{

AmpEditor::AmpEditor (AmpProcessor& p)
    : juce::AudioProcessorEditor (&p), amp (p), chrome (p), strip (p), faceplate (p),
      gateStrip (p.gateKeyDb, p.gateMeterDb, p.inClip, *p.apvts.getParameter (params::gateThreshold),
                 *p.apvts.getParameter (params::inTrim), *p.apvts.getParameter (params::gateOn),
                 *p.apvts.getParameter (params::gateDecay)),
      outStrip (p.outDb, p.outClip, *p.apvts.getParameter (params::outTrim),
                *p.apvts.getParameter (params::limiterCeiling)),
      gateBadge ("Gate", *p.apvts.getParameter (params::gateOn), p.gateMeterDb, 40.0f),
      limitBadge ("Limit", *p.apvts.getParameter (params::limiterOn), p.limiterGrDb, 6.0f),
      tunerStrip (p.tunerEar), footer (p), demoStrip (p)
{
    addAndMakeVisible (chrome);
    addAndMakeVisible (strip);
    addAndMakeVisible (faceplate);
    addAndMakeVisible (gateStrip);
    addAndMakeVisible (outStrip);
    addAndMakeVisible (gateBadge);
    addAndMakeVisible (limitBadge);
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
    // The badges' clicks mean MENU — the whole device in one pick; the gate's zoom is the
    // menu's OPEN item, for the day the deep face is wanted.
    gateBadge.onOpen = [this] (juce::Point<int> pos) { gateStrip.showPresetMenu (pos); };
    gateStrip.onOpenZoom = [this] { strip.onOpen (ChainLink::gate); };

    // The measurement, projected: the overlay reads the strip's own trace.
    learnOverlay.trace      = &gateStrip.learnTraceRef();
    learnOverlay.totalTicks = GateStrip::learnTotalTicks;
    gateStrip.onLearnBegin  = [this] { learnOverlay.begin(); };
    gateStrip.onLearnDone   = [this] (const juce::String& v) { learnOverlay.finish (v); };
    addChildComponent (learnOverlay);
    limitBadge.onOpen = [this] (juce::Point<int> pos) { showLimiterMenu (pos); };

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

void AmpEditor::showLimiterMenu (juce::Point<int> screenPos)
{
    auto* on   = amp.apvts.getParameter (params::limiterOn);
    auto* ceil = amp.apvts.getParameter (params::limiterCeiling);

    const bool  isOn = on->getValue() > 0.5f;
    const float c    = ceil->convertFrom0to1 (ceil->getValue());

    const auto matches = [&] (float v) { return isOn && std::abs (c - v) < 0.05f; };

    juce::PopupMenu m;
    m.addSectionHeader ("LIMITER");
    m.addItem (1, "OFF",           true, ! isOn);
    m.addItem (2, "SAFETY  -0.3",  true, matches (-0.3f));
    m.addItem (3, "NORMAL  -1.0",  true, matches (-1.0f));
    m.addItem (4, "TIGHT   -3.0",  true, matches (-3.0f));

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetScreenArea ({ screenPos.x, screenPos.y, 1, 1 }),
                     [this, on, ceil] (int r)
                     {
                         if (r == 0)
                             return;

                         const auto set = [] (juce::RangedAudioParameter* p, float plain)
                         {
                             p->beginChangeGesture();
                             p->setValueNotifyingHost (p->convertTo0to1 (plain));
                             p->endChangeGesture();
                         };

                         // OFF is a toggle here too — a menu that can only kill is no menu.
                         if (r == 1)
                         {
                             set (on, on->getValue() > 0.5f ? 0.0f : 1.0f);
                             return;
                         }

                         set (on, 1.0f);
                         set (ceil, r == 2 ? -0.3f : r == 3 ? -1.0f : -3.0f);
                     });
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
    gateBadge.setBounds (margin, tunerY, TallyBadge::designWidth, TunerStrip::designHeight);
    gateBadge.setTransform (zoom);

    limitBadge.setBounds (margin + FaceplateView::designWidth - TallyBadge::designWidth, tunerY,
                          TallyBadge::designWidth, TunerStrip::designHeight);
    limitBadge.setTransform (zoom);

    // The learn sheet: half the plugin, centred over the faceplate.
    learnOverlay.setBounds (margin + FaceplateView::designWidth / 6, faceplateY + 60,
                            FaceplateView::designWidth * 2 / 3, 330);
    learnOverlay.setTransform (zoom);

    tunerStrip.setBounds (margin + TallyBadge::designWidth + chromeGap, tunerY,
                          FaceplateView::designWidth - 2 * (TallyBadge::designWidth + chromeGap),
                          TunerStrip::designHeight);
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
