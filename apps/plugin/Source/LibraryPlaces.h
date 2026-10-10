#pragma once

#include "library/Backup.h"
#include "library/Packs.h"

#include <juce_core/juce_core.h>

namespace osp::plugin
{

/** Where this installation keeps what packs and backups touch (Stage 10):
    - the sample store;
    - `<Library>/Packs` for installed packs and `<Library>/Factory` for factory packs;
    - Documents/OSP Presets and Starting States;
    - `<Library>/settings.json` and the presets' Favourites.txt. */
library::PackPlaces packPlaces();
library::BackupPlaces backupPlaces();
/** Factory packs shipped with the plugin: the bundle's Resources/Factory (OSP_FACTORY_PACKS_DIR overrides). */
juce::File factoryPackSource();
/** A file or folder to the system Trash (recoverable), for removals and replaced files. */
bool moveToSystemTrash (const std::filesystem::path& path);

} // namespace osp::plugin
