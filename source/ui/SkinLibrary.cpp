#include "SkinLibrary.h"
#include "BinaryData.h"

//==============================================================================
namespace
{
    const juce::Identifier kName   { "name" };
    const juce::Identifier kColors { "colors" };

    const juce::Identifier kSlots[8] = {
        "background", "panel", "primary", "secondary",
        "muted", "zoneLit", "zoneIdle", "marker"
    };

    juce::Colour parseColour (const juce::var& v)
    {
        juce::String s = v.toString().trim();
        if (s.startsWithIgnoreCase ("0x"))
            s = s.substring (2);
        return juce::Colour { static_cast<std::uint32_t> (s.getHexValue32()) };
    }
}

//==============================================================================
void SkinLibrary::reload()
{
    skins_.clear();

    // Bundled defaults (compiled in).
    addFromJson (juce::String (BinaryData::dark_json,
                               static_cast<size_t> (BinaryData::dark_jsonSize)));
    addFromJson (juce::String (BinaryData::eighties_neon_json,
                               static_cast<size_t> (BinaryData::eighties_neon_jsonSize)));

    // Imported skins from the per-user folder.
    const auto folder = userFolder();
    for (const auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.json"))
        addFromJson (f.loadFileAsString());
}

bool SkinLibrary::addFromJson (const juce::String& text)
{
    const juce::var root = juce::JSON::parse (text);
    if (! root.isObject())
        return false;

    const juce::String name = root.getProperty (kName, {}).toString();
    const juce::var colorsVar = root.getProperty (kColors, {});
    if (name.isEmpty() || ! colorsVar.isObject())
        return false;

    // Require every colour slot to be present.
    for (const auto& slot : kSlots)
        if (! colorsVar.hasProperty (slot))
            return false;

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

    // A user skin with a duplicate name overrides the bundled one.
    for (auto& existing : skins_)
        if (existing.name == skin.name)
        {
            existing = std::move (skin);
            return true;
        }

    skins_.push_back (std::move (skin));
    return true;
}

const Skin* SkinLibrary::findByName (const juce::String& name) const
{
    for (const auto& s : skins_)
        if (s.name == name)
            return &s;
    return nullptr;
}

juce::File SkinLibrary::userFolder()
{
    auto base   = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
    auto folder = base.getChildFile ("ModfingerTuner/skins");
    folder.createDirectory();
    return folder;
}

juce::String SkinLibrary::nameFromJson (const juce::String& text)
{
    const juce::var root = juce::JSON::parse (text);
    return root.getProperty (kName, {}).toString();
}
