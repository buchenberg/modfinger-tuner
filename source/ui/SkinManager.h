#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

#include "TunerPalette.h"
#include "SkinLibrary.h"

class ModfingerTunerAudioProcessor;

//==============================================================================
/** Manages the runtime skin system for the editor: the skin library, the
    in‑UI selector button, the themed popup menu, the async import flow, and
    pushing colour changes into the LookAndFeel + reference label.

    Owns the `juce::TextButton` so the editor just addAndMakeVisible's it and
    positions it in `resized()`.
*/
class SkinManager
{
public:
    SkinManager();

    /** One‑time initialisation after construction — wires up the processor,
        LookAndFeel, and reference label that the manager will theme. */
    void initialise (ModfingerTunerAudioProcessor& processor,
                     juce::LookAndFeel& laf,
                     juce::Label& refLabel);

    /** Reload the skin library (bundled + user) and apply the saved skin. */
    void reloadAndApply();

    /** Look up `name` in the library, apply its palette colours, and store
        it as the active skin.  Falls back to "80s Neon" → first available. */
    void applySkinByName (const juce::String& name);

    /** Show the themed skin‑selector popup (rescans the user folder first). */
    void showSkinMenu();

    /** Persist `name` in plugin state and apply it. */
    void selectSkin (const juce::String& name);

    /** Open an async file‑chooser to import a .json skin into the user folder,
        then reload and select it. */
    void importSkin();

    /** The actual JUCE button — the editor must `addAndMakeVisible` and
        position it. */
    juce::TextButton& getButton() { return skinButton_; }

    /** Currently active colour palette (updated by applySkinByName). */
    const TunerPalette& getPalette() const { return palette_; }

    /** Name of the currently applied skin (for change detection). */
    const juce::String& activeSkinName() const { return activeSkinName_; }

private:
    juce::TextButton skinButton_;
    std::unique_ptr<juce::FileChooser> skinFileChooser_;
    SkinLibrary skinLibrary_;

    juce::String   activeSkinName_;
    TunerPalette   palette_;

    // Non‑owning references set by initialise().
    ModfingerTunerAudioProcessor* processor_ = nullptr;
    juce::LookAndFeel*            laf_       = nullptr;
    juce::Label*                  refLabel_  = nullptr;
};
