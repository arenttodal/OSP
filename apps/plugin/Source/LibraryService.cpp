#include "LibraryService.h"

#include "io/AudioFileIO.h"
#include "library/SoundImport.h"

namespace osp::plugin
{

LibraryService::LibraryService (juce::File sampleStoreDirectory) : store (std::move (sampleStoreDirectory))
{
    // A copy an interrupted import left behind (older than an hour: another instance may be
    // copying one right now) is cleared once per start.
    post ([dir = store] (library::Catalog&) {
        library::clearInterruptedImports (dir.getFullPathName().toStdString(), std::chrono::hours (1));
    });
}

LibraryService::~LibraryService()
{
    // Finish what is queued (each job is short), then close the connection.
    waitUntilIdle (10000);
    pool.removeAllJobs (true, 2000);
    db.reset();
}

library::Catalog* LibraryService::catalog()
{
    if (! opened)
    {
        opened = true;
        std::string error;
        db = library::Catalog::open (library::Catalog::defaultFile(), error);
        if (db == nullptr)
        {
            const juce::ScopedLock lock (reasonLock);
            reason = juce::String (error);
        }
    }
    return db.get();
}

void LibraryService::post (std::function<void (library::Catalog&)> task)
{
    pool.addJob ([this, task = std::move (task)] {
        if (auto* c = catalog())
            task (*c);
    });
}

bool LibraryService::runAndWait (std::function<void (library::Catalog&)> task, int milliseconds)
{
    auto done = std::make_shared<juce::WaitableEvent>();
    auto ran = std::make_shared<std::atomic<bool>> (false);
    pool.addJob ([this, task = std::move (task), done, ran] {
        if (auto* c = catalog())
        {
            task (*c);
            ran->store (true);
        }
        done->signal();
    });
    return done->wait (milliseconds) && ran->load();
}

bool LibraryService::waitUntilIdle (int milliseconds)
{
    const auto until = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (milliseconds);
    while (pool.getNumJobs() > 0)
    {
        if (juce::Time::getMillisecondCounter() > until)
            return false;
        juce::Thread::sleep (2);
    }
    return true;
}

void LibraryService::deliver()
{
    std::vector<std::function<void()>> ready;
    {
        const std::lock_guard<std::mutex> lock (finishedMutex);
        ready.swap (finished);
    }
    for (auto& f : ready)
        f();
}

juce::String LibraryService::unavailableReason() const
{
    const juce::ScopedLock lock (reasonLock);
    return reason;
}

void LibraryService::soundLoaded (const LoadedInstrument& instrument, int layer, bool byUser)
{
    // Every file the layer plays: the single sound, or each member of a multi-sample set.
    struct One
    {
        std::string hash, filename, originalPath;
    };
    std::vector<One> files;
    if (instrument.memberFiles.empty())
        files.push_back ({ instrument.contentHash, instrument.filename, instrument.originalPath });
    else
        for (const auto& m : instrument.memberFiles)
            files.push_back ({ m.contentHash, m.filename, m.originalPath });
    post ([files, layer, byUser, dir = store] (library::Catalog& c) {
        for (const auto& f : files)
        {
            if (f.hash.empty())
                continue;
            library::SoundInfo info;
            info.contentHash = f.hash;
            info.managed = true;
            info.provenance = "unknown";   // the user says otherwise (export asks)
            if (const auto stored = library::findStored (dir.getFullPathName().toStdString(), library::hexOf (f.hash)))
            {
                io::AudioFileInfo header;
                std::string error;
                if (io::readAudioFileInfo (*stored, header, error))
                {
                    info.sampleRate = header.sampleRate;
                    info.channels = header.channels;
                    info.bitDepth = header.bitDepth;
                    info.frames = header.frames;
                    info.durationSeconds = header.sampleRate > 0.0 ? static_cast<double> (header.frames) / header.sampleRate : 0.0;
                }
                std::error_code ec;
                info.fileSize = static_cast<std::int64_t> (std::filesystem::file_size (*stored, ec));
                info.format = stored->extension().string().empty() ? std::string() : stored->extension().string().substr (1);
            }
            library::Asset asset;
            asset.origin = library::Origin::user;
            asset.name = juce::File::createFileWithoutCheckingPath (juce::String (f.filename)).getFileNameWithoutExtension().toStdString();
            if (asset.name.empty())
                asset.name = "Sound";
            const auto place = f.originalPath.empty() ? std::string ("(store)") : f.originalPath;
            if (const auto added = c.addImportedSound (asset, info, place, "original"); added && byUser)
                c.recordUse (added->id, "loaded", layer);
        }
    });
}

void LibraryService::presetSaved (const juce::File& file, library::AssetType type, std::vector<std::string> soundHashes, int layers, int stateVersion)
{
    post ([path = file.getFullPathName().toStdString(), name = file.getFileNameWithoutExtension().toStdString(), type, hashes = std::move (soundHashes), layers,
           stateVersion] (library::Catalog& c) {
        library::Asset asset;
        asset.type = type;
        asset.origin = library::Origin::user;
        asset.name = name;
        library::PresetInfo info;
        info.file = path;
        info.root = "user";
        info.stateVersion = stateVersion;
        info.layers = layers;
        info.soundHashes = hashes;
        c.savePreset (asset, info);
    });
}

} // namespace osp::plugin
