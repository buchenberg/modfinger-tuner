#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "TunerPalette.h"
#include <vector>

//==============================================================================
/** Runtime, data-driven skin loader.

    Skins are JSON files. Bundled defaults ship inside the plugin (BinaryData);
    additional/imported skins live in a per-user folder. reload() refreshes the
    list, so an imported skin shows up the next time the selector opens — no
    recompile, no restart.
*/
struct Skin
{
    juce::String   name;
    TunerPalette   palette;
};

class SkinLibrary
{
public:
    /** Re-read bundled defaults + the user folder. */
    void reload();

    const std::vector<Skin>& skins() const { return skins_; }
    const Skin* findByName (const juce::String& name) const;

    /** Per-user writable folder imported skins are stored in. */
    static juce::File userFolder();

    /** Extract the "name" field from a skin JSON blob (for post-import feedback). */
    static juce::String nameFromJson (const juce::String& text);

private:
    bool addFromJson (const juce::String& text);   // returns false if invalid
    std::vector<Skin> skins_;
};
