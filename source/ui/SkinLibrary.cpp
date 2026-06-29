#include "SkinLibrary.h"
#include "BinaryData.h"

//==============================================================================
//  Anonymous helpers for JSON parsing.
//==============================================================================
namespace
{
    const juce::Identifier kName   { "name" };
    const juce::Identifier kColors { "colors" };

    // The 8 fixed colour slots; must match the field order of TunerPalette.
    const juce::Identifier kSlots[8] = {
        "background", "panel", "primary", "secondary",
        "muted", "zoneLit", "zoneIdle", "marker"
    };

    /** Parse a JSON colour value.  Accepts hex strings like "0xff1a1a1e"
        (case‑insensitive "0x" prefix stripped before hex decoding). */
    juce::Colour parseColour (const juce::var& v)
    {
        juce::String s = v.toString().trim();
        if (s.startsWithIgnoreCase ("0x"))
            s = s.substring (2);
        return juce::Colour { static_cast<std::uint32_t> (s.getHexValue32()) };
    }
}

//==============================================================================
//  reload  — clear the library and re‑scan everything.
//  Bundled defaults come from BinaryData (compiled‑in via CMake); user skins
//  come from the per‑user writable folder.  Called once at construction and
//  every time the skin menu opens, so folder‑dropped files appear instantly.
//==============================================================================
void SkinLibrary::reload()
{
    skins_.clear();

    // ── Bundled defaults (BinaryData, never overwritten by reload) ─
    addFromJson (juce::String (BinaryData::dark_json,
                               static_cast<size_t> (BinaryData::dark_jsonSize)));
    addFromJson (juce::String (BinaryData::eighties_neon_json,
                               static_cast<size_t> (BinaryData::eighties_neon_jsonSize)));

    // ── User skins from the per‑user folder ───────────────────────
    const auto folder = userFolder();
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
        addFromJson (f.loadFileAsString());
}

//==============================================================================
//  addFromJson  — parse a single JSON skin blob and append or replace it.
//  Returns false if the JSON is invalid (missing name, missing colours, etc.).
//==============================================================================
bool SkinLibrary::addFromJson (const juce::String& text)
{
    const juce::var root = juce::JSON::parse (text);
    if (! root.isObject())
        return false;

    const juce::String name = root.getProperty (kName, {}).toString();
    const juce::var colorsVar = root.getProperty (kColors, {});
    if (name.isEmpty() || ! colorsVar.isObject())
        return false;

    // Every colour slot is required — a missing slot means the skin is malformed.
    for (const auto& slot : kSlots)
        if (! colorsVar.hasProperty (slot))
            return false;

    // Build the skin from the JSON colour values.
    Skin skin;
    skin.name = name;
    skin.palette.background = parseColour (colorsVar.getProperty (kSlots[0], {}));
    skin.palette.panel      = parseColour (colorsVar.getProperty (kSlots[1], {}));
    skin.palette.primary    = parseColour (colorsVar.getProperty (kSlots[2], {}));
    skin.palette.secondary  = parseColour (colorsVar.getProperty (kSlots[3], {}));
    skin.palette.muted      = parseColour (colorsVar.getProperty (kSlots[4], {}));
    skin.palette.zoneLit    = parseColour (colorsVar.getProperty (kSlots[5], {}));
    skin.palette.zoneIdle   = parseColour (colorsVar.getProperty (kSlots[6], {}));
    skin.palette.marker     = parseColour (colorsVar.getProperty (kSlots[7], {}));

    // A user skin whose `name` matches a bundled skin overrides it.
    // This lets you replace "80s Neon" with your own version.
    for (auto& existing : skins_)
        if (existing.name == skin.name)
        {
            existing = std::move (skin);
            return true;
        }

    skins_.push_back (std::move (skin));
    return true;
}

//==============================================================================
//  findByName  — linear search (the skin list is small, typically ≤10 items).
//==============================================================================
const Skin* SkinLibrary::findByName (const juce::String& name) const
{
    for (const auto& s : skins_)
        if (s.name == name)
            return &s;
    return nullptr;
}

//==============================================================================
//  userFolder  — return the platform‑specific per‑user skins folder, creating
//  it if absent.  Paths:
//    macOS   ~/Library/Application Support/ModfingerTuner/skins/
//    Windows %APPDATA%\ModfingerTuner\skins\
//    Linux   ~/.config/ModfingerTuner/skins/
//==============================================================================
juce::File SkinLibrary::userFolder()
{
    // userApplicationDataDirectory resolves to the platform‑standard per‑user
    // writable data folder.
    auto base   = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    auto folder = base.getChildFile ("ModfingerTuner/skins");
    folder.createDirectory();        // no‑op if it already exists
    return folder;
}

//==============================================================================
//  nameFromJson  — parse just the "name" field from a JSON blob.
//  Used by the import flow so we can auto‑select the newly imported skin.
//==============================================================================
juce::String SkinLibrary::nameFromJson (const juce::String& text)
{
    const juce::var root = juce::JSON::parse (text);
    return root.getProperty (kName, {}).toString();
}
