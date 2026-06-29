//==============================================================================
//  PluginEditor  — timer-driven display state machine, skin management,
//  and the JUCE graphics paint routine for the tuner UI.
//==============================================================================

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/Pitch.h"

//==============================================================================
//  textWidth  — measure the advance width of a single-line string using
//  GlyphArrangement (the recommended replacement for the deprecated
//  Font::getStringWidthFloat).
//==============================================================================
float ModfingerTunerAudioProcessorEditor::textWidth (const juce::Font& font, const juce::String& text)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (font, text, 0.0f, 0.0f);
    return ga.getBoundingBox (0, ga.getNumGlyphs(), true).getWidth();
}

//==============================================================================
//  Constructor  — build the UI, load skins, start the 25 Hz timer.
//==============================================================================
ModfingerTunerAudioProcessorEditor::ModfingerTunerAudioProcessorEditor (ModfingerTunerAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef_ (p)
{
    setLookAndFeel (&tunerLAF_);
    setSize (380, 300);         // fixed-size editor — no resizing

    // ── Reference pitch label ─────────────────────────────────────
    // Click-to-edit label showing "440.0 Hz".  Edits are validated
    // against the actual parameter range (no duplicated magic numbers).
    referenceLabel_.setEditable (true);
    referenceLabel_.setJustificationType (juce::Justification::centred);
    referenceLabel_.setFont (juce::Font { juce::FontOptions { 14.0f } });

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

    // ── Skin selector button ──────────────────────────────────────
    // Bottom-right pill that opens a themed popup listing all runtime
    // skins (bundled + user-imported).
    skinButton_.onClick = [this] { showSkinMenu(); };
    addAndMakeVisible (skinButton_);

    // Load the runtime skin library (bundled defaults + user folder)
    // and apply whichever skin the processor last saved.
    skinLibrary_.reload();
    applySkinByName (processorRef_.getSkinName());

    // ── 25 Hz display timer ───────────────────────────────────────
    // Paints the UI at ~25 fps.  The pYIN detector runs at ~43 Hz,
    // so the timer will occasionally see the same detection twice —
    // the exponential smoothing handles this gracefully.
    startTimerHz (25);
}

ModfingerTunerAudioProcessorEditor::~ModfingerTunerAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

//==============================================================================
//  applySkinByName  — look up a skin by name in the runtime library and push
//  its palette into the LookAndFeel, the reference label, the skin-button
//  colours, and the popup-menu theme.  Falls back to "80s Neon" → first skin.
//==============================================================================
void ModfingerTunerAudioProcessorEditor::applySkinByName (const juce::String& name)
{
    const Skin* skin = skinLibrary_.findByName (name);

    // Fallback chain: requested name → "80s Neon" → first available skin.
    if (skin == nullptr) skin = skinLibrary_.findByName ("80s Neon");
    if (skin == nullptr && ! skinLibrary_.skins().empty())
        skin = &skinLibrary_.skins().front();
    if (skin == nullptr) return;   // no skins in library at all

    activeSkinName_ = skin->name;
    palette_        = skin->palette;

    // Apply to the LookAndFeel (window background) and the reference label.
    tunerLAF_.setColour (juce::ResizableWindow::backgroundColourId, palette_.background);
    referenceLabel_.setColour (juce::Label::textColourId,                  palette_.secondary);
    referenceLabel_.setColour (juce::Label::textWhenEditingColourId,       palette_.primary);
    referenceLabel_.setColour (juce::Label::backgroundWhenEditingColourId, palette_.panel);
    referenceLabel_.setColour (juce::Label::outlineWhenEditingColourId,    palette_.primary);
    referenceLabel_.repaint();

    // Skin selector button — a subtle pill matching the palette.
    skinButton_.setColour (juce::TextButton::buttonColourId,    palette_.panel);
    skinButton_.setColour (juce::TextButton::buttonOnColourId,  palette_.panel);
    skinButton_.setColour (juce::TextButton::textColourOffId,   palette_.secondary);
    skinButton_.setColour (juce::TextButton::textColourOnId,    palette_.primary);
    skinButton_.setButtonText ("Skin: " + activeSkinName_);

    // The themed popup menu (the button inherits the editor's LookAndFeel,
    // so setting colours on `tunerLAF_` themes the popup too).
    tunerLAF_.setColour (juce::PopupMenu::backgroundColourId,            palette_.panel);
    tunerLAF_.setColour (juce::PopupMenu::textColourId,                  palette_.secondary);
    tunerLAF_.setColour (juce::PopupMenu::highlightedBackgroundColourId, palette_.primary.withAlpha (0.25f));
    tunerLAF_.setColour (juce::PopupMenu::highlightedTextColourId,       palette_.primary);

    skinButton_.repaint();
    repaint();
}

//==============================================================================
//  showSkinMenu  — reload the skin library (to pick up newly-dropped files)
//  and show a themed popup listing all skins, with Import and Open-dolder
//  actions below a separator.
//==============================================================================
void ModfingerTunerAudioProcessorEditor::showSkinMenu()
{
    skinLibrary_.reload();   // scan the user folder for new .json files

    juce::PopupMenu menu;
    for (const auto& skin : skinLibrary_.skins())
        menu.addItem (skin.name, true, skin.name == activeSkinName_,
                      [this, n = skin.name] { selectSkin (n); });

    menu.addSeparator();
    menu.addItem ("Import skin...",        true, false, [this] { importSkin(); });
    menu.addItem ("Open skins folder...",  true, false,
                  [] { SkinLibrary::userFolder().startAsProcess(); });

    juce::PopupMenu::Options opts;
    opts = opts.withTargetComponent (&skinButton_).withMinimumWidth (150);
    menu.showMenuAsync (opts);
}

//==============================================================================
//  selectSkin  — persist a skin by name into the processor state and apply it.
//==============================================================================
void ModfingerTunerAudioProcessorEditor::selectSkin (const juce::String& name)
{
    processorRef_.setSkinName (name);
    applySkinByName (name);
}

//==============================================================================
//  importSkin  — open a native file-chooser, copy the selected .json into the
//  user skins folder, reload, and select it.  Runs async (no modal loop).
//==============================================================================
void ModfingerTunerAudioProcessorEditor::importSkin()
{
    skinFileChooser_ = std::make_unique<juce::FileChooser> ("Import Skin",
                                                            SkinLibrary::userFolder(), "*.json");
    skinFileChooser_->launchAsync (juce::FileBrowserComponent::openMode
                                        | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            const juce::File src = fc.getResult();
            if (src == juce::File{})
                return;                         // user cancelled

            // Copy the file into the user folder (overwrites if same name).
            const juce::File dst = SkinLibrary::userFolder().getChildFile (src.getFileName());
            src.copyFileTo (dst);

            // Parse the imported file to get the skin name, then reload and
            // select it so the UI updates immediately.
            const juce::String name = SkinLibrary::nameFromJson (src.loadFileAsString());
            skinLibrary_.reload();
            if (name.isNotEmpty())
                selectSkin (name);
        });
}

//==============================================================================
//  timerCallback  — called every ~40 ms by the JUCE message thread.
//
//  Responsibilities:
//    1.  Read the atomic frequency / aperiodicity from the processor.
//    2.  Run the 3-state display machine (tracking / holding / listening).
//    3.  Ease the hold-fade alpha so the transition into/out of "holding" is smooth.
//    4.  Update the reference-pitch label (unless the user is editing it).
//    5.  Detect host-initiated skin changes (e.g. state restore) and re-apply.
//    6.  Trigger repaint().
//==============================================================================
void ModfingerTunerAudioProcessorEditor::timerCallback()
{
    // ── React to host-initiated skin changes ──────────────────────
    // If the host restored a saved state (setStateInformation), the
    // skinName value may differ from what we last applied.
    const juce::String skinName = processorRef_.getSkinName();
    if (skinName != activeSkinName_)
        applySkinByName (skinName);

    // ── Read atomic readouts from the audio thread ────────────────
    const float rawFreq         = processorRef_.getDisplayFrequency();
    const float rawAperiodicity = processorRef_.getDisplayAperiodicity();
    cachedRefHz_ = processorRef_.apvts.getRawParameterValue ("reference")->load();

    // ════════════════════════════════════════════════════════════════
    //  Display state machine
    //
    //  tracking   →  a pitch is detected: smooth the frequency, cache
    //                note/octave/cents, refresh the 5 s hold timer.
    //
    //  holding    →  the pitch stopped but we keep showing the last
    //                note for a few seconds before giving up.
    //                (holds are great for brief drop-outs.)
    //
    //  listening  →  the hold expired — clear the frequency and show
    //                "listening…".
    //
    //  Hold duration = kHoldTicks = 125 ticks ≈ 5 s at 25 Hz.
    // ════════════════════════════════════════════════════════════════
    if (rawFreq > 0.0f)
    {
        // Exponential smoothing: α = 0.25 gives fast attack (~4 ticks to
        // reach 63 % of the new value), with gentle release handled by the
        // hold timer below rather than a separate decay coefficient.
        smoothedFreq_ += 0.25f * (rawFreq - smoothedFreq_);

        // Derive the displayed note & cents on the UI thread (no String races).
        cachedNote_         = pitch::noteInfoFromFrequency (smoothedFreq_, cachedRefHz_);
        cachedCents_        = pitch::centsFromFrequency (smoothedFreq_, cachedRefHz_);
        cachedAperiodicity_ = rawAperiodicity;   // frozen during hold so the note doesn't flash dim
        holdTicksRemaining_ = kHoldTicks;        // reset the hold timer
        displayState_       = DisplayState::tracking;
    }
    else if (holdTicksRemaining_ > 0)
    {
        --holdTicksRemaining_;
        displayState_ = DisplayState::holding;
    }
    else
    {
        smoothedFreq_ = 0.0f;                    // hold expired — clear
        displayState_ = DisplayState::listening;
    }

    // ── Ease the hold-fade alpha ──────────────────────────────────
    // When the state transitions from tracking→holding, holdFadeAlpha_
    // eases from 1.0 toward kHoldFadeAlpha (0.35) over ~200 ms
    // (α=0.2 per 40 ms tick).  The reverse direction (holding→tracking)
    // eases back to 1.0 when the pitch resumes.
    const float fadeTarget = (displayState_ == DisplayState::holding) ? kHoldFadeAlpha : 1.0f;
    holdFadeAlpha_ += 0.2f * (fadeTarget - holdFadeAlpha_);

    // ── Reference-label text (update if the user isn't typing) ────
    if (! referenceLabel_.isBeingEdited())
        referenceLabel_.setText (juce::String (cachedRefHz_, 1) + " Hz",
                                 juce::dontSendNotification);

    repaint();
}

//==============================================================================
//  paint  — draw the full tuner UI every frame.
//
//  Layout (top → bottom, y coordinates):
//    Note + octave    →  16 – 112  (96 px box, 96 pt bold font)
//    Cents numeric    → 116 – 136  (just below the note/octave)
//    Cents bar        → 156 – 182  (26 px tall, 260 px wide, centred)
//    Needle           →  142 – 188  (extends above & below the bar)
//    Cents markings   → 188 – 202  (-50 / 0 / +50 labels)
//    Frequency readout → 210 – 228
//    Reference label  → 260 – 284  (bottom-left)
//    Skin button      → 260 – 284  (bottom-right)
//
//  The `fade` variable (holdFadeAlpha_) dims the entire readout during the
//  "holding" state.  It is eased for a smooth visual transition.
//==============================================================================
void ModfingerTunerAudioProcessorEditor::paint (juce::Graphics& g)
{
    const auto w = static_cast<float> (getWidth());

    // Background fill — the skin palette drives this colour.
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
        // Idle state — a muted "listening..." placeholder where the note goes.
        g.setColour (palette_.muted);
        g.setFont (juce::Font { juce::FontOptions { 22.0f } });
        g.drawText ("listening...", juce::Rectangle<float> (0, noteTop, w, noteH + 24.0f),
                    juce::Justification::centred, false);
    }
    else
    {
        // Active readout — note letter + octave number.
        juce::Font noteFont   { juce::FontOptions { 96.0f, juce::Font::bold } };
        juce::Font octaveFont { juce::FontOptions { 36.0f, juce::Font::bold } };

        const juce::String noteText   (cachedNote_.name);
        const juce::String octaveText (cachedNote_.octave);
        const float noteW   = textWidth (noteFont, noteText);
        const float octaveW = textWidth (octaveFont, octaveText);
        const float gap     = 6.0f;          // between note and octave

        // Note letter centred horizontally; octave to its right (normal text flow).
        const float noteX   = (w - noteW) * 0.5f;
        const float octaveX = noteX + noteW + gap;

        // Note letter — the largest element on screen (96 pt bold).
        g.setColour (noteColour.withAlpha (fade));
        g.setFont (noteFont);
        g.drawText (noteText, juce::Rectangle<float> (noteX, noteTop, noteW, noteH),
                    juce::Justification::centredLeft, false);

        // Octave number — smaller, same hue, slightly dimmer (90 % alpha).
        g.setColour (noteColour.withAlpha (0.9f * fade));
        g.setFont (octaveFont);
        g.drawText (octaveText, juce::Rectangle<float> (octaveX, noteTop, octaveW, noteH),
                    juce::Justification::centredLeft, false);

        // ── Cents numeric, just below the note/octave ─────────────
        // e.g. "+3.9 ¢" in the primary colour at 80 % alpha.
        g.setColour (noteColour.withAlpha (0.8f * fade));
        g.setFont (juce::Font { juce::FontOptions { 16.0f, juce::Font::bold } });
        juce::String centsText;
        centsText << (cachedCents_ >= 0.0 ? "+" : "") << juce::String (cachedCents_, 1)
                  << " " << juce::String::charToString (static_cast<juce::juce_wchar> (0x00A2));  // cents symbol
        g.drawText (centsText, juce::Rectangle<float> (0, centsValueY, w, 20.0f),
                    juce::Justification::centred, false);
    }

    // ════════════════════════════════════════════════════════════════
    //  Cents bar  — the horizontal tuning indicator.
    //
    //  The bar spans ±50 cents.  The green zone (±5 cents) lights up
    //  when the needle is inside it *and* we are actively tracking
    //  (not holding, not listening).  The needle is a 2 px vertical
    //  line with a small downward-pointing triangle.
    // ════════════════════════════════════════════════════════════════
    const auto barW      = 260.0f;          // bar width in px
    const auto barH      = 26.0f;           // bar height in px
    const auto barX      = (w - barW) * 0.5f;
    const auto barY      = 156.0f;
    const auto barCentre = barX + barW * 0.5f;

    // Bar background — a rounded rectangle slightly larger than the bar.
    g.setColour (palette_.panel);
    g.fillRoundedRectangle (barX - 2.0f, barY - 2.0f, barW + 4.0f, barH + 4.0f, 4.0f);

    // Green in-tune zone (±5 cents).
    // Lit (0.85 α) during active tracking; dim (0.22 α) otherwise.
    const float zonePixelsPerCent = (barW * 0.5f) / 50.0f;  // half-bar = 50 cents
    const auto greenLeft  = barCentre - 5.0f * zonePixelsPerCent;
    const auto greenRight = barCentre + 5.0f * zonePixelsPerCent;
    const bool inTune = displayState_ == DisplayState::tracking
                        && std::abs (static_cast<float> (cachedCents_)) <= 5.0f;
    const auto zoneColour = inTune ? palette_.zoneLit.withAlpha (0.85f)
                                   : palette_.zoneIdle.withAlpha (0.22f);
    g.setColour (zoneColour);
    g.fillRect (greenLeft, barY, greenRight - greenLeft, barH);

    // Center tick (0-cent marker).
    g.setColour (palette_.marker);
    g.drawVerticalLine (static_cast<int> (barCentre), barY - 4.0f, barY + barH + 4.0f);

    // Cents markings — small muted labels below the bar.
    g.setColour (palette_.muted);
    g.setFont (juce::Font { juce::FontOptions { 9.0f } });
    g.drawText ("-50",  juce::Rectangle<float> (barX - 14, barY + barH + 6, 28, 14),
                juce::Justification::centred, false);
    g.drawText ("0",    juce::Rectangle<float> (barCentre - 14, barY + barH + 6, 28, 14),
                juce::Justification::centred, false);
    g.drawText ("+50",  juce::Rectangle<float> (barX + barW - 14, barY + barH + 6, 28, 14),
                juce::Justification::centred, false);

    // ════════════════════════════════════════════════════════════════
    //  Needle + frequency readout  — only while a note is shown
    //  (tracking or holding).  Hidden during "listening."
    // ════════════════════════════════════════════════════════════════
    if (showNote)
    {
        // Cents are clamped to ±50 (needle stays inside the bar).
        const float clampedCents = juce::jlimit (-50.0f, 50.0f, static_cast<float> (cachedCents_));
        const float needleX = barCentre + clampedCents * zonePixelsPerCent;

        // Needle alpha: bright when confident, dim when aperiodic.
        const auto needleAlpha = confident ? 1.0f : 0.4f;
        g.setColour (palette_.primary.withAlpha (needleAlpha * fade));

        // 2 px vertical line straddling the bar.
        g.drawVerticalLine (static_cast<int> (needleX), barY - 6.0f, barY + barH + 6.0f);
        g.drawVerticalLine (static_cast<int> (needleX - 1.0f), barY - 6.0f, barY + barH + 6.0f);

        // Small downward-pointing triangle just above the bar.
        juce::Path needleTip;
        needleTip.addTriangle (needleX, barY - 6.0f,
                               needleX - 5.0f, barY - 14.0f,
                               needleX + 5.0f, barY - 14.0f);
        g.fillPath (needleTip);

        // Frequency readout — e.g. "440.0 Hz" in the secondary colour.
        g.setColour (palette_.secondary.withAlpha (fade));
        g.setFont (juce::Font { juce::FontOptions { 13.0f } });
        g.drawText (juce::String (smoothedFreq_, 1) + " Hz",
                    juce::Rectangle<float> (0, barY + barH + 28.0f, w, 18.0f),
                    juce::Justification::centred, false);
    }
}

//==============================================================================
//  resized  — position the fixed-size controls at the bottom.
//==============================================================================
void ModfingerTunerAudioProcessorEditor::resized()
{
    // Bottom row: reference label (left) + skin selector (right).
    const int y = getHeight() - 40;
    referenceLabel_.setBounds (20, y, 120, 24);
    skinButton_.setBounds (getWidth() - 140, y, 120, 24);
}
