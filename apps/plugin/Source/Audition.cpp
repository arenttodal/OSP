#include "Audition.h"

#include "io/AudioFileIO.h"
#include "library/SoundImport.h"

namespace osp::plugin
{

// LibrarySettings --------------------------------------------------------------

juce::File LibrarySettings::file()
{
    return juce::File (juce::String (library::Catalog::defaultFile().parent_path().string())).getChildFile ("settings.json");
}

LibrarySettings LibrarySettings::load()
{
    LibrarySettings s;
    const auto json = juce::JSON::parse (file().loadFileAsString());
    if (auto* object = json.getDynamicObject())
    {
        // A newer build's settings: read what this one knows, never rewrite the rest away
        // (save() only happens on a user change).
        if (object->hasProperty ("previewGainDb"))
            s.previewGainDb = juce::jlimit (-60.0f, 6.0f, static_cast<float> (object->getProperty ("previewGainDb")));
    }
    return s;
}

bool LibrarySettings::save() const
{
    auto existing = juce::JSON::parse (file().loadFileAsString());
    auto* object = existing.getDynamicObject();
    if (object == nullptr)
    {
        existing = juce::var (new juce::DynamicObject());
        object = existing.getDynamicObject();
    }
    object->setProperty ("schemaVersion", std::max (schemaVersion, static_cast<int> (object->getProperty ("schemaVersion"))));
    object->setProperty ("previewGainDb", previewGainDb);
    const auto target = file();
    target.getParentDirectory().createDirectory();
    // Written beside, then moved over: a crash never leaves half a settings file.
    const auto partial = target.getSiblingFile (target.getFileName() + ".partial");
    return partial.replaceWithText (juce::JSON::toString (existing)) && partial.moveFileTo (target);
}

// Audition ---------------------------------------------------------------------

Audition::Audition (library::PreviewEngine& e, LibraryService& l, juce::File sampleStore, LoadHook load)
    : engine (e), library (l), store (std::move (sampleStore)), loadHook (std::move (load)), settings (LibrarySettings::load())
{
    engine.setGainDb (settings.previewGainDb);
}

Audition::~Audition()
{
    // Background work that is already running finishes first; anything after sees `alive`
    // gone and does nothing.
    alive.reset();
    library.waitUntilIdle (10000);
    decoder.removeAllJobs (true, 10000);
}

const Audition::Slot& Audition::slot (int index) const
{
    return slots[static_cast<std::size_t> (juce::jlimit (0, library::PreviewEngine::numSlots - 1, index))];
}

bool Audition::trayEmpty() const
{
    for (int s = 0; s < numTraySlots; ++s)
        if (! slots[static_cast<std::size_t> (s)].assetId.empty())
            return false;
    return true;
}

void Audition::post (std::function<void()> onMessageThread)
{
    const std::lock_guard<std::mutex> lock (doneMutex);
    finishedWork.push_back (std::move (onMessageThread));
}

void Audition::deliver()
{
    std::vector<std::function<void()>> ready;
    {
        const std::lock_guard<std::mutex> lock (doneMutex);
        ready.swap (finishedWork);
    }
    for (auto& f : ready)
        f();
}

void Audition::changed()
{
    if (onChange != nullptr)
        onChange();
}

Audition::Found Audition::find (library::Catalog& catalog, const std::filesystem::path& storeDir, const std::string& assetId)
{
    Found found;
    const auto asset = catalog.asset (assetId);
    const auto sound = catalog.sound (assetId);
    if (! asset || ! sound)
    {
        found.error = "This sound is no longer in the Library.";
        return found;
    }
    found.name = juce::String (asset->name);
    found.root = sound->rootMidi;
    found.contentHash = sound->contentHash;
    const auto where = library::resolveSound (catalog, storeDir, sound->contentHash);
    if (! where.file)
    {
        found.error = found.name + " cannot be found"
                    + (where.missing.empty() ? juce::String (".") : " (last seen at " + juce::String (where.missing.front().string()) + ").");
        return found;
    }
    found.ok = true;
    found.file = juce::File (juce::String (where.file->string()));
    found.filename = found.name + found.file.getFileExtension();
    // Where it came from: the place the bytes were found, unless that is the managed copy;
    // then the first place it was seen that still exists (else the first one known).
    if (where.where != "store")
        found.originalPath = found.file.getFullPathName();
    else
    {
        for (const auto& place : catalog.locations (sound->contentHash))
            if (place != "(store)" && juce::File (juce::String (place)).existsAsFile())
            {
                found.originalPath = juce::String (place);
                break;
            }
        if (found.originalPath.isEmpty())
            for (const auto& place : catalog.locations (sound->contentHash))
                if (place != "(store)")
                {
                    found.originalPath = juce::String (place);
                    break;
                }
    }
    return found;
}

void Audition::prepare (int slotIndex, const std::string& assetId, std::optional<juce::File> file, bool playWhenReady)
{
    auto& s = slots[static_cast<std::size_t> (slotIndex)];
    s = {};
    s.assetId = assetId;
    s.loading = true;
    s.name = file ? file->getFileNameWithoutExtension() : juce::String();
    const auto request = ++requests[static_cast<std::size_t> (slotIndex)];
    changed();

    std::weak_ptr<bool> token = alive;
    // Decoding and the root estimate on the preview's own thread; the result goes to the
    // engine on the message thread (only the newest request of the slot counts).
    auto decode = [this, token, slotIndex, request, assetId, playWhenReady] (Found found) {
        decoder.addJob ([this, token, slotIndex, request, assetId, playWhenReady, found] {
            if (token.expired())
                return;
            std::shared_ptr<const library::PreviewSound> sound;
            juce::String error = found.error;
            if (found.ok)
            {
                io::LoadOptions options;
                options.maxSeconds = 60.0;   // a preview: the first minute of anything longer
                auto decoded = io::loadAudioFile (found.file.getFullPathName().toStdString(), options);
                if (decoded.ok)
                    sound = library::makePreviewSound (std::move (decoded.audio), found.root, assetId, found.name.toStdString());
                else
                    error = found.name + " could not be read: " + juce::String (decoded.error);
            }
            post ([this, token, slotIndex, request, playWhenReady, found, sound, error] {
                if (token.expired() || requests[static_cast<std::size_t> (slotIndex)] != request)
                    return;
                auto& target = slots[static_cast<std::size_t> (slotIndex)];
                target.loading = false;
                target.error = error;
                target.sound = sound;
                target.file = found.ok ? found.file : juce::File();
                if (found.name.isNotEmpty())
                    target.name = found.name;
                engine.setSound (slotIndex, sound);
                if (sound != nullptr && playWhenReady)
                    engine.play (1u << slotIndex);
                changed();
            });
        });
    };

    if (file)
    {
        Found found;
        found.ok = file->existsAsFile();
        found.file = *file;
        found.name = file->getFileNameWithoutExtension();
        if (! found.ok)
            found.error = file->getFileName() + " is not there any more.";
        decode (found);
        return;
    }
    const auto storeDir = std::filesystem::path (store.getFullPathName().toStdString());
    library.post ([token, storeDir, assetId, decode] (library::Catalog& catalog) {
        if (token.expired())
            return;
        decode (find (catalog, storeDir, assetId));
    });
}

void Audition::preview (const std::string& assetId)
{
    prepare (library::PreviewEngine::browserSlot, assetId, std::nullopt, true);
}

void Audition::cue (const std::string& assetId)
{
    if (slots[library::PreviewEngine::browserSlot].assetId == assetId && (slots[library::PreviewEngine::browserSlot].loading
                                                                          || slots[library::PreviewEngine::browserSlot].sound != nullptr))
        return;   // already there (or on its way)
    prepare (library::PreviewEngine::browserSlot, assetId, std::nullopt, false);
}

const std::vector<float>* Audition::overview (const std::string& assetId)
{
    if (const auto it = overviews.find (assetId); it != overviews.end())
        return &it->second;
    if (assetId.empty() || overviewsPending.count (assetId) > 0)
        return nullptr;
    overviewsPending.insert (assetId);
    std::weak_ptr<bool> token = alive;
    const auto storeDir = std::filesystem::path (store.getFullPathName().toStdString());
    library.post ([this, token, storeDir, assetId] (library::Catalog& catalog) {
        if (token.expired())
            return;
        const auto found = find (catalog, storeDir, assetId);
        decoder.addJob ([this, token, assetId, found] {
            if (token.expired())
                return;
            std::vector<float> bins (overviewBins, 0.0f);
            if (found.ok)
            {
                io::LoadOptions options;
                options.maxSeconds = 30.0;
                const auto decoded = io::loadAudioFile (found.file.getFullPathName().toStdString(), options);
                const auto frames = decoded.ok ? decoded.audio.numFrames() : 0;
                for (int b = 0; b < overviewBins && frames > 0; ++b)
                {
                    const auto from = frames * b / overviewBins, to = std::max (from + 1, frames * (b + 1) / overviewBins);
                    float peak = 0.0f;
                    for (const auto& channel : decoded.audio.channels)
                        for (auto i = from; i < std::min (to, frames); ++i)
                            peak = std::max (peak, std::abs (channel[static_cast<std::size_t> (i)]));
                    bins[static_cast<std::size_t> (b)] = std::min (1.0f, peak);
                }
            }
            post ([this, assetId, bins] {
                overviews[assetId] = bins;
                overviewsPending.erase (assetId);
                changed();
            });
        });
    });
    return nullptr;
}

void Audition::previewFile (const juce::File& file)
{
    prepare (library::PreviewEngine::browserSlot, {}, file, true);
}

void Audition::setSlot (int slotIndex, const std::string& assetId)
{
    if (slotIndex < 0 || slotIndex >= numTraySlots)
        return;
    prepare (slotIndex, assetId, std::nullopt, false);
}

void Audition::clearSlot (int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= library::PreviewEngine::numSlots)
        return;
    ++requests[static_cast<std::size_t> (slotIndex)];
    slots[static_cast<std::size_t> (slotIndex)] = {};
    engine.setSound (slotIndex, nullptr);
    changed();
}

void Audition::play (unsigned slotMask)
{
    engine.play (slotMask);
}

void Audition::stop()
{
    engine.stop();
}

void Audition::setGainDb (float db)
{
    engine.setGainDb (db);
    settings.previewGainDb = engine.gainDb();
    settings.save();
}

void Audition::loadIntoLayer (const std::string& assetId, int layer, Done done)
{
    std::weak_ptr<bool> token = alive;
    const auto storeDir = std::filesystem::path (store.getFullPathName().toStdString());
    library.post ([this, token, storeDir, assetId, layer, done] (library::Catalog& catalog) {
        if (token.expired())
            return;
        const auto found = find (catalog, storeDir, assetId);
        post ([this, token, found, layer, done] {
            if (token.expired())
                return;
            const bool ok = found.ok && loadHook != nullptr && loadHook ({ { layer, found.file, found.filename, found.originalPath, found.root } }, found.name);
            if (done != nullptr)
                done (ok, ok ? found.name + " loaded into " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + layer)) : found.error);
        });
    });
}

void Audition::commit (Done done)
{
    std::vector<std::pair<int, std::string>> wanted;
    for (int s = 0; s < numTraySlots; ++s)
        if (! slots[static_cast<std::size_t> (s)].assetId.empty())
            wanted.emplace_back (s, slots[static_cast<std::size_t> (s)].assetId);
    if (wanted.empty())
    {
        if (done != nullptr)
            done (false, "The audition tray is empty: put sounds into A, B or C first.");
        return;
    }
    std::weak_ptr<bool> token = alive;
    const auto storeDir = std::filesystem::path (store.getFullPathName().toStdString());
    library.post ([this, token, storeDir, wanted, done] (library::Catalog& catalog) {
        if (token.expired())
            return;
        // Every sound resolved before anything changes: one missing sound and the instrument
        // stays exactly as it is.
        std::vector<LayerLoad> files;
        juce::StringArray names, problems;
        for (const auto& [layer, id] : wanted)
        {
            const auto found = find (catalog, storeDir, id);
            if (! found.ok)
                problems.add (found.error);
            else
            {
                files.push_back ({ layer, found.file, found.filename, found.originalPath, found.root });
                names.add (found.name);
            }
        }
        post ([this, token, files, names, problems, done] {
            if (token.expired())
                return;
            if (! problems.isEmpty())
            {
                if (done != nullptr)
                    done (false, "Nothing was changed. " + problems.joinIntoString (" "));
                return;
            }
            const bool ok = loadHook != nullptr && loadHook (files, names.joinIntoString (" + "));
            if (done != nullptr)
                done (ok, ok ? "Loaded " + names.joinIntoString (", ") : juce::String ("The sounds could not be loaded."));
        });
    });
}

bool Audition::waitUntilIdle (int milliseconds)
{
    const auto until = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (milliseconds);
    while (juce::Time::getMillisecondCounter() < until)
    {
        library.waitUntilIdle (milliseconds);
        if (decoder.getNumJobs() == 0)
        {
            library.waitUntilIdle (milliseconds);   // a decode may have been queued by a catalog job
            if (decoder.getNumJobs() == 0)
                return true;
        }
        juce::Thread::sleep (2);
    }
    return false;
}

} // namespace osp::plugin
