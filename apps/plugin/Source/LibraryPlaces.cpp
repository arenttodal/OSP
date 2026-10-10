#include "LibraryPlaces.h"

#include "Audition.h"
#include "InstrumentLoader.h"
#include "PluginProcessor.h"

#include <cstdlib>

namespace osp::plugin
{

namespace
{
    std::filesystem::path pathOf (const juce::File& f) { return std::filesystem::path (f.getFullPathName().toStdString()); }
    std::filesystem::path libraryDir() { return library::Catalog::defaultFile().parent_path(); }
}

library::PackPlaces packPlaces()
{
    return { pathOf (SampleStore::defaultDirectory()), libraryDir() / "Packs", libraryDir() / "Factory", JucePlugin_VersionString };
}

library::BackupPlaces backupPlaces()
{
    library::BackupPlaces p;
    p.storeDir = pathOf (SampleStore::defaultDirectory());
    p.presetsDir = pathOf (OspAudioProcessor::presetFolder());
    p.templatesDir = pathOf (OspAudioProcessor::startingStateFolder());
    p.packsDir = libraryDir() / "Packs";
    p.settingsFile = pathOf (LibrarySettings::file());
    p.favouritesFile = pathOf (OspAudioProcessor::presetFolder().getChildFile ("Favourites.txt"));
    p.pluginVersion = JucePlugin_VersionString;
    return p;
}

juce::File factoryPackSource()
{
    if (const char* dir = std::getenv ("OSP_FACTORY_PACKS_DIR"); dir != nullptr && *dir != 0)
        return juce::File (juce::String::fromUTF8 (dir));
    // The plugin's own bundle (.component / .vst3 / .app): Contents/Resources/Factory.
    const auto bundle = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getParentDirectory();
    return bundle.getChildFile ("Resources").getChildFile ("Factory");
}

bool moveToSystemTrash (const std::filesystem::path& path)
{
    return juce::File (juce::String (path.string())).moveToTrash();
}

} // namespace osp::plugin
