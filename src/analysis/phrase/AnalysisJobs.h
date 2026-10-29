#pragma once

#include "analysis/phrase/PhraseAnalysis.h"
#include "core/AudioData.h"
#include "model/NoteAtlas.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace osp::phrase
{

/**
    Phrase analysis as background jobs (unified engine Phase 4).

    The entry point takes an immutable asset - decoded audio and its SourceAsset record (content
    hash, origin, name) - from any source: a dropped file, a Library asset, a buffer snapshot,
    later a finalised live capture. Nothing here knows about files or a file picker.

    - Bounded concurrency: at most `workers` analyses run at once (worker threads owned by the
      queue); the rest wait, in order.
    - Bounded memory: the audio of queued and running jobs together may not exceed
      `maxQueuedSeconds` (a request beyond it is refused, never silently dropped), and one
      analysis covers at most AnalysisSettings::maxAnalysisSeconds of its recording.
    - Simultaneous requests for the same recording and settings (content hash + analysis
      version + settings digest) share one analysis: the later ones attach to the running or
      queued job and get the same result.
    - Cancellation: per job; a cancelled job reports cancelled and never calls back with a
      result. Destroying the queue cancels everything and joins its threads.
    - Progress: queued, running (0..1), finished, failed, cancelled.

    Never call from the audio thread: submit() allocates and takes a lock. Callbacks run on a
    worker thread.
*/
class AnalysisQueue
{
public:
    enum class State { queued, running, finished, failed, cancelled, unknown };

    struct Request
    {
        std::shared_ptr<const AudioData> audio;
        atlas::SourceAsset source;     ///< contentHash identifies the recording
        AnalysisSettings settings;
    };

    using JobId = std::uint64_t;
    /** Called once per submitted job (also for jobs that attached to another): its result. */
    using Callback = std::function<void (JobId, const AnalysisResult&)>;

    struct Status
    {
        State state = State::unknown;
        float progress = 0.0f;   ///< 0..1 while running, 1 when finished
    };

    explicit AnalysisQueue (int workers = 1, double maxQueuedSeconds = 600.0);
    ~AnalysisQueue();

    AnalysisQueue (const AnalysisQueue&) = delete;
    AnalysisQueue& operator= (const AnalysisQueue&) = delete;

    /** Queues an analysis. Returns no id (and calls nothing) when the request is unusable or
        the queue's memory bound would be passed; `error` says why. */
    std::optional<JobId> submit (Request request, Callback onDone, std::string* error = nullptr);

    /** Cancels a job (queued: removed; running: stopped at its next check). Its callback is
        not called. Attached requests of the same analysis keep it running unless all cancel. */
    void cancel (JobId id);

    Status status (JobId id) const;

    /** The cache key of a request: content hash, analysis version and settings digest. */
    static std::string keyOf (const atlas::SourceAsset& source, const AnalysisSettings& settings);

    /** Blocks until no job is queued or running (tests, shutdown paths). */
    void waitUntilIdle();

    int workers() const noexcept { return static_cast<int> (threads.size()); }

private:
    struct Job
    {
        std::string key;
        Request request;
        std::vector<std::pair<JobId, Callback>> waiting;   ///< every request sharing this analysis
        std::atomic<bool> cancel { false };
        std::atomic<float> progress { 0.0f };
        bool running = false;
    };

    void run();
    double queuedSecondsLocked() const;

    mutable std::mutex mutex;
    std::condition_variable wake, idle;
    std::deque<std::shared_ptr<Job>> pending;
    std::map<std::string, std::shared_ptr<Job>> byKey;   ///< queued or running, by cache key
    std::map<JobId, std::shared_ptr<Job>> byId;          ///< queued or running, by request
    std::map<JobId, State> finished;                     ///< finished / failed / cancelled requests
    std::vector<std::thread> threads;
    double maxSeconds;
    JobId nextId = 1;
    int busy = 0;
    bool stopping = false;
};

} // namespace osp::phrase
