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

    // Apply the current skin (colours come from the "skin" parameter).
    applySkin (processorRef_.apvts.getRawParameterValue ("skin")->load());

    // Timer for display updates (~25 Hz)
    startTimerHz (25);
}

ModfingerTunerAudioProcessorEditor::~ModfingerTunerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
void ModfingerTunerAudioProcessorEditor::applySkin (int index)
{
    palette_   = (index == 0) ? TunerPalette::darkOrange() : TunerPalette::eightiesNeon();
    skinIndex_ = index;

    tunerLAF_.setColour (juce::ResizableWindow::backgroundColourId, palette_.background);
    referenceLabel_.setColour (juce::Label::textColourId,                  palette_.secondary);
    referenceLabel_.setColour (juce::Label::textWhenEditingColourId,       palette_.primary);
    referenceLabel_.setColour (juce::Label::backgroundWhenEditingColourId, palette_.panel);
    referenceLabel_.setColour (juce::Label::outlineWhenEditingColourId,    palette_.primary);
    referenceLabel_.repaint();
}

//==============================================================================
void ModfingerTunerAudioProcessorEditor::timerCallback()
{
    // React to skin changes coming from the host.
    const int skinIdx = static_cast<int> (processorRef_.apvts.getRawParameterValue ("skin")->load());
    if (skinIdx != skinIndex_)
        applySkin (skinIdx);

    const float rawFreq         = processorRef_.getDisplayFrequency();
    const float rawAperiodicity = processorRef_.getDisplayAperiodicity();
    cachedRefHz_ = processorRef_.apvts.getRawParameterValue ("reference")->load();

    // Three readout states: track a live pitch, hold the last note (dimmed) for
    // a few seconds after it drops, then go idle ("listening…").
    if (rawFreq > 0.0f)
    {
        smoothedFreq_ += 0.25f * (rawFreq - smoothedFreq_);

        // Derive note & cents from the smoothed frequency on the UI thread.
        cachedNote_         = pitch::noteInfoFromFrequency (smoothedFreq_, cachedRefHz_);
        cachedCents_        = pitch::centsFromFrequency (smoothedFreq_, cachedRefHz_);
        cachedAperiodicity_ = rawAperiodicity;   // frozen during hold so it stays confident
        holdTicksRemaining_ = kHoldTicks;
        displayState_       = DisplayState::tracking;
    }
    else if (holdTicksRemaining_ > 0)
    {
        --holdTicksRemaining_;
        displayState_ = DisplayState::holding;
    }
    else
    {
        smoothedFreq_ = 0.0f;
        displayState_ = DisplayState::listening;
    }

    // Ease the dim applied while holding: full while tracking, faded while holding.
    const float fadeTarget = (displayState_ == DisplayState::holding) ? kHoldFadeAlpha : 1.0f;
    holdFadeAlpha_ += 0.2f * (fadeTarget - holdFadeAlpha_);

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
    g.fillAll (palette_.background);

    const bool showNote = displayState_ != DisplayState::listening;   // tracking or holding
    const float fade    = holdFadeAlpha_;                             // ~1.0 tracking, dim while holding
    const bool confident = cachedAperiodicity_ < kConfidentAperiodicity;
    const auto noteColour = confident ? palette_.primary
                                      : palette_.muted;

    const float noteTop = 16.0f;
    const float noteH   = 96.0f;
    const float centsValueY = noteTop + noteH + 4.0f;   // cents number sits just below the note

    // ── Note + octave (or "listening…") ───────────────────────────
    if (! showNote)
    {
        g.setColour (palette_.muted);
        g.setFont (juce::Font { juce::FontOptions { 22.0f } });
        g.drawText ("listening...", juce::Rectangle<float> (0, noteTop, w, noteH + 24.0f),
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

        g.setColour (noteColour.withAlpha (fade));
        g.setFont (noteFont);
        g.drawText (noteText, juce::Rectangle<float> (noteX, noteTop, noteW, noteH),
                    juce::Justification::centredLeft, false);

        g.setColour (noteColour.withAlpha (0.9f * fade));
        g.setFont (octaveFont);
        g.drawText (octaveText, juce::Rectangle<float> (octaveX, noteTop, octaveW, noteH),
                    juce::Justification::centredLeft, false);

        // ── Cents numeric, just below the note/octave ─────────────
        g.setColour (noteColour.withAlpha (0.8f * fade));
        g.setFont (juce::Font { juce::FontOptions { 16.0f, juce::Font::bold } });
        juce::String centsText;
        centsText << (cachedCents_ >= 0.0 ? "+" : "") << juce::String (cachedCents_, 1) << " \u00A2";  // cents symbol
        g.drawText (centsText, juce::Rectangle<float> (0, centsValueY, w, 20.0f),
                    juce::Justification::centred, false);
    }

    // ── Cents bar ──────────────────────────────────────────────────
    const auto barW      = 260.0f;
    const auto barH      = 26.0f;
    const auto barX      = (w - barW) * 0.5f;
    const auto barY      = 156.0f;
    const auto barCentre = barX + barW * 0.5f;

    // Bar background
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (barX - 2.0f, barY - 2.0f, barW + 4.0f, barH + 4.0f, 4.0f);

    // Green in-tune zone (±5 cents) — lights up when the needle is inside it
    const float zonePixelsPerCent = (barW * 0.5f) / 50.0f;  // half bar = 50 cents
    const auto greenLeft  = barCentre - 5.0f * zonePixelsPerCent;
    const auto greenRight = barCentre + 5.0f * zonePixelsPerCent;
    const bool inTune = displayState_ == DisplayState::tracking
                        && std::abs (static_cast<float> (cachedCents_)) <= 5.0f;
    const auto zoneColour = inTune ? palette_.zoneLit.withAlpha (0.85f)
                                   : palette_.zoneIdle.withAlpha (0.22f);
    g.setColour (zoneColour);
    g.fillRect (greenLeft, barY, greenRight - greenLeft, barH);

    // Center marker
    g.setColour (palette_.marker);
    g.drawVerticalLine (static_cast<int> (barCentre), barY - 4.0f, barY + barH + 4.0f);

    // Cents markings
    g.setColour (palette_.muted);
    g.setFont (juce::Font { juce::FontOptions { 9.0f } });
    g.drawText ("-50",  juce::Rectangle<float> (barX - 14, barY + barH + 6, 28, 14),
                juce::Justification::centred, false);
    g.drawText ("0",    juce::Rectangle<float> (barCentre - 14, barY + barH + 6, 28, 14),
                juce::Justification::centred, false);
    g.drawText ("+50",  juce::Rectangle<float> (barX + barW - 14, barY + barH + 6, 28, 14),
                juce::Justification::centred, false);

    // ── Needle + frequency readout — shown (dimmed) while a note is shown ──
    if (showNote)
    {
        const float clampedCents = juce::jlimit (-50.0f, 50.0f, static_cast<float> (cachedCents_));
        const float needleX = barCentre + clampedCents * zonePixelsPerCent;

        const auto needleAlpha = confident ? 1.0f : 0.4f;
        g.setColour (palette_.primary.withAlpha (needleAlpha * fade));
        g.drawVerticalLine (static_cast<int> (needleX), barY - 6.0f, barY + barH + 6.0f);
        g.drawVerticalLine (static_cast<int> (needleX - 1.0f), barY - 6.0f, barY + barH + 6.0f);  // 2px thick

        // Needle triangle indicator
        juce::Path needleTip;
        needleTip.addTriangle (needleX, barY - 6.0f,
                               needleX - 5.0f, barY - 14.0f,
                               needleX + 5.0f, barY - 14.0f);
        g.fillPath (needleTip);

        // Frequency readout
        g.setColour (palette_.secondary.withAlpha (fade));
        g.setFont (juce::Font { juce::FontOptions { 13.0f } });
        g.drawText (juce::String (smoothedFreq_, 1) + " Hz",
                    juce::Rectangle<float> (0, barY + barH + 28.0f, w, 18.0f),
                    juce::Justification::centred, false);
    }
}

void ModfingerTunerAudioProcessorEditor::resized()
{
    // Reference pitch label at bottom center
    referenceLabel_.setBounds ((getWidth() - 100) / 2, getHeight() - 40, 100, 24);
}
