#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Pitch.h"

//==============================================================================
float ModfingerTunerAudioProcessorEditor::textWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (font, text, 0.0f, 0.0f);
    return ga.getBoundingBox (0, ga.getNumGlyphs(), true).getWidth();
}

//==============================================================================
ModfingerTunerAudioProcessorEditor::ModfingerTunerAudioProcessorEditor (ModfingerTunerAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef_ (p)
{
    setLookAndFeel (&tunerLAF_);
    setSize (380, 300);

    // ── Reference pitch label (editable) ───────────────────────────
    referenceLabel_.setEditable (true);
    referenceLabel_.setColour (juce::Label::textColourId, juce::Colour (0xffa0a0a8));
    referenceLabel_.setColour (juce::Label::textWhenEditingColourId, juce::Colour (0xffe0743b));
    referenceLabel_.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff24242a));
    referenceLabel_.setColour (juce::Label::outlineWhenEditingColourId, juce::Colour (0xffe0743b));
    referenceLabel_.setJustificationType (juce::Justification::centred);
    referenceLabel_.setFont (juce::Font { juce::FontOptions { 14.0f } });

    // Commit an edited reference through the real parameter API so the host is
    // notified and the value is validated against the parameter's own range
    // (no duplicated magic numbers).
    referenceLabel_.onTextChange = [this] ()
    {
        if (auto* param = processorRef_.apvts.getParameter ("reference"))
        {
            const auto range  = param->getNormalisableRange().getRange();
            const float newRef = referenceLabel_.getText().getFloatValue();
            if (newRef >= range.getStart() && newRef <= range.getEnd())
                param->setValueNotifyingHost (param->convertTo0to1 (newRef));
        }
    };

    cachedRefHz_ = processorRef_.apvts.getRawParameterValue ("reference")->load();
    referenceLabel_.setText (juce::String (cachedRefHz_, 1) + " Hz", juce::dontSendNotification);
    addAndMakeVisible (referenceLabel_);

    // Timer for display updates (~25 Hz)
    startTimerHz (25);
}

ModfingerTunerAudioProcessorEditor::~ModfingerTunerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void ModfingerTunerAudioProcessorEditor::timerCallback()
{
    const float rawFreq         = processorRef_.getDisplayFrequency();
    const float rawAperiodicity = processorRef_.getDisplayAperiodicity();
    cachedRefHz_                = processorRef_.apvts.getRawParameterValue ("reference")->load();
    cachedAperiodicity_         = rawAperiodicity;

    // Exponential smoothing: fast attack when a pitch is present, gentle
    // release otherwise. Snap to silence below the floor so the UI returns to
    // the "listening" state instead of asymptotically approaching zero.
    if (rawFreq > 0.0f)
    {
        smoothedFreq_ += 0.25f * (rawFreq - smoothedFreq_);

        // Derive note & cents from the smoothed frequency on the UI thread.
        cachedNote_  = pitch::noteInfoFromFrequency (smoothedFreq_, cachedRefHz_);
        cachedCents_ = pitch::centsFromFrequency (smoothedFreq_, cachedRefHz_);
    }
    else
    {
        smoothedFreq_ *= 0.9f;
        if (smoothedFreq_ < kSilenceFloorHz)
            smoothedFreq_ = 0.0f;
    }

    // Update reference label if not being edited
    if (! referenceLabel_.isBeingEdited())
        referenceLabel_.setText (juce::String (cachedRefHz_, 1) + " Hz",
                                 juce::dontSendNotification);

    repaint();
}

//==============================================================================
void ModfingerTunerAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto w = static_cast<float> (getWidth());

    // Background
    g.fillAll (juce::Colour (0xff1a1a1e));

    const bool hasSignal = smoothedFreq_ > 0.0f;
    const bool confident = cachedAperiodicity_ < kConfidentAperiodicity;
    const auto noteColour = confident ? juce::Colour (0xffe0743b)
                                      : juce::Colour (0xff5a5a60);

    const float noteTop = 16.0f;
    const float noteH   = 110.0f;

    if (! hasSignal)
    {
        // Waiting for a note — the rest of the UI still draws, but note + octave stay hidden
        g.setColour (juce::Colour (0xff5a5a62));
        g.setFont (juce::Font { juce::FontOptions { 22.0f } });
        g.drawText ("listening...", juce::Rectangle<float> (0, noteTop, w, noteH),
                    juce::Justification::centred, false);
    }
    else
    {
        // ── Note name + octave ────────────────────────────────────
        juce::Font noteFont   { juce::FontOptions { 96.0f, juce::Font::bold } };
        juce::Font octaveFont { juce::FontOptions { 36.0f, juce::Font::bold } };

        const juce::String noteText   (cachedNote_.name);
        const juce::String octaveText (cachedNote_.octave);
        const float noteW   = textWidth (noteFont, noteText);
        const float octaveW = textWidth (octaveFont, octaveText);
        const float gap     = 6.0f;

        // Note letter centered; octave sits to its right (normal text flow)
        const float noteX   = (w - noteW) * 0.5f;
        const float octaveX = noteX + noteW + gap;

        g.setColour (noteColour);
        g.setFont (noteFont);
        g.drawText (noteText, juce::Rectangle<float> (noteX, noteTop, noteW, noteH),
                    juce::Justification::centredLeft, false);

        g.setColour (noteColour.withAlpha (0.9f));
        g.setFont (octaveFont);
        g.drawText (octaveText, juce::Rectangle<float> (octaveX, noteTop, octaveW, noteH),
                    juce::Justification::centredLeft, false);
    }

    // ── Cents bar ──────────────────────────────────────────────────
    const auto barW      = 260.0f;
    const auto barH      = 28.0f;
    const auto barX      = (w - barW) * 0.5f;
    const auto barY      = 150.0f;
    const auto barCentre = barX + barW * 0.5f;

    // Bar background
    g.setColour (juce::Colour (0xff2a2a30));
    g.fillRoundedRectangle (barX - 2.0f, barY - 2.0f, barW + 4.0f, barH + 4.0f, 4.0f);

    // Green in-tune zone (±5 cents)
    const float zonePixelsPerCent = (barW * 0.5f) / 50.0f;  // half bar = 50 cents
    const auto greenLeft  = barCentre - 5.0f * zonePixelsPerCent;
    const auto greenRight = barCentre + 5.0f * zonePixelsPerCent;
    g.setColour (juce::Colour (0xff22aa44).withAlpha (0.3f));
    g.fillRect (greenLeft, barY, greenRight - greenLeft, barH);

    // Center marker
    g.setColour (juce::Colour (0xff888890));
    g.drawVerticalLine (static_cast<int> (barCentre), barY - 4.0f, barY + barH + 4.0f);

    // Cents markings
    g.setColour (juce::Colour (0xff5a5a62));
    g.setFont (juce::Font { juce::FontOptions { 9.0f } });
    g.drawText ("-50",  juce::Rectangle<float> (barX - 14, barY + barH + 6, 28, 14),
                juce::Justification::centred, false);
    g.drawText ("0",    juce::Rectangle<float> (barCentre - 14, barY + barH + 6, 28, 14),
                juce::Justification::centred, false);
    g.drawText ("+50",  juce::Rectangle<float> (barX + barW - 14, barY + barH + 6, 28, 14),
                juce::Justification::centred, false);

    // Cents derived from the displayed (smoothed) frequency; 0 while silent.
    const float clampedCents = juce::jlimit (-50.0f, 50.0f, static_cast<float> (cachedCents_));
    const float needleX = barCentre + clampedCents * zonePixelsPerCent;

    // Needle line
    const auto needleAlpha = confident ? 1.0f : 0.4f;
    g.setColour (juce::Colour (0xffe0743b).withAlpha (needleAlpha));
    g.drawVerticalLine (static_cast<int> (needleX), barY - 6.0f, barY + barH + 6.0f);
    g.drawVerticalLine (static_cast<int> (needleX - 1.0f), barY - 6.0f, barY + barH + 6.0f);  // 2px thick

    // Needle triangle indicator
    juce::Path needleTip;
    needleTip.addTriangle (needleX, barY - 6.0f,
                           needleX - 5.0f, barY - 14.0f,
                           needleX + 5.0f, barY - 14.0f);
    g.fillPath (needleTip);

    // ── Cents numeric display ──────────────────────────────────────
    g.setColour (noteColour.withAlpha (0.8f));
    g.setFont (juce::Font { juce::FontOptions { 16.0f, juce::Font::bold } });
    juce::String centsText;
    centsText << (cachedCents_ >= 0.0 ? "+" : "") << juce::String (cachedCents_, 1) << " \u00A2";  // cents symbol
    g.drawText (centsText, juce::Rectangle<float> (0, barY + barH + 20, w, 22),
                juce::Justification::centred, false);

    // ── Frequency readout ──────────────────────────────────────────
    g.setColour (juce::Colour (0xffa0a0a8));
    g.setFont (juce::Font { juce::FontOptions { 13.0f } });
    g.drawText (juce::String (smoothedFreq_, 1) + " Hz",
                juce::Rectangle<float> (0, barY + barH + 40, w, 18),
                juce::Justification::centred, false);
}

void ModfingerTunerAudioProcessorEditor::resized()
{
    // Reference pitch label at bottom center
    referenceLabel_.setBounds ((getWidth() - 100) / 2, getHeight() - 40, 100, 24);
}
