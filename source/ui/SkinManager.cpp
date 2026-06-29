//==============================================================================
//  SkinManager  — runtime skin system for the editor.
//  See SkinManager.h for the public API overview.
//==============================================================================

#include "SkinManager.h"
#include "../PluginProcessor.h"              // for getSkinName / setSkinName

//==============================================================================
SkinManager::SkinManager()
{
    // The skin button will be configured by applySkinByName (colours + text)
    // and positioned by the editor's resized().
    skinButton_.onClick = [this] { showSkinMenu(); };
}

//==============================================================================
void SkinManager::initialise (ModfingerTunerAudioProcessor& processor,
                               juce::LookAndFeel& laf,
                               juce::Label& refLabel)
{
    processor_ = &processor;
    laf_       = &laf;
    refLabel_  = &refLabel;
}

//==============================================================================
void SkinManager::reloadAndApply()
{
    skinLibrary_.reload();
    applySkinByName (processor_->getSkinName());
}

//==============================================================================
void SkinManager::applySkinByName (const juce::String& name)
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
    laf_->setColour (juce::ResizableWindow::backgroundColourId, palette_.background);
    refLabel_->setColour (juce::Label::textColourId,                  palette_.secondary);
    refLabel_->setColour (juce::Label::textWhenEditingColourId,       palette_.primary);
    refLabel_->setColour (juce::Label::backgroundWhenEditingColourId, palette_.panel);
    refLabel_->setColour (juce::Label::outlineWhenEditingColourId,    palette_.primary);
    refLabel_->repaint();

    // Skin selector button — a subtle pill matching the palette.
    skinButton_.setColour (juce::TextButton::buttonColourId,    palette_.panel);
    skinButton_.setColour (juce::TextButton::buttonOnColourId,  palette_.panel);
    skinButton_.setColour (juce::TextButton::textColourOffId,   palette_.secondary);
    skinButton_.setColour (juce::TextButton::textColourOnId,    palette_.primary);
    skinButton_.setButtonText ("Skin: " + activeSkinName_);

    // Themed popup menu — sets colours on the editor‑wide LookAndFeel.
    laf_->setColour (juce::PopupMenu::backgroundColourId,            palette_.panel);
    laf_->setColour (juce::PopupMenu::textColourId,                  palette_.secondary);
    laf_->setColour (juce::PopupMenu::highlightedBackgroundColourId, palette_.primary.withAlpha (0.25f));
    laf_->setColour (juce::PopupMenu::highlightedTextColourId,       palette_.primary);

    skinButton_.repaint();
}

//==============================================================================
void SkinManager::showSkinMenu()
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
void SkinManager::selectSkin (const juce::String& name)
{
    processor_->setSkinName (name);
    applySkinByName (name);
}

//==============================================================================
void SkinManager::importSkin()
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

            const juce::File dst = SkinLibrary::userFolder().getChildFile (src.getFileName());
            src.copyFileTo (dst);

            const juce::String name = SkinLibrary::nameFromJson (src.loadFileAsString());
            skinLibrary_.reload();
            if (name.isNotEmpty())
                selectSkin (name);
        });
}
