#include "analysis/phrase/AnalysisJobs.h"

#include <algorithm>

namespace osp::phrase
{

AnalysisQueue::AnalysisQueue (int workerCount, double maxQueuedSeconds)
    : maxSeconds (std::max (1.0, maxQueuedSeconds))
{
    for (int i = 0; i < std::clamp (workerCount, 1, 8); ++i)
        threads.emplace_back ([this] { run(); });
}

AnalysisQueue::~AnalysisQueue()
{
    {
        const std::lock_guard<std::mutex> lock (mutex);
        stopping = true;
        for (auto& [id, job] : byId)
            job->cancel = true;
        pending.clear();
    }
    wake.notify_all();
    for (auto& t : threads)
        t.join();
}

std::string AnalysisQueue::keyOf (const atlas::SourceAsset& source, const AnalysisSettings& settings)
{
    return source.contentHash + "|v" + std::to_string (analysisVersion) + "|" + settings.digest();
}

double AnalysisQueue::queuedSecondsLocked() const
{
    double seconds = 0.0;
    for (const auto& [key, job] : byKey)
        seconds += std::min (job->request.audio->durationSeconds(), job->request.settings.maxAnalysisSeconds);
    return seconds;
}

std::optional<AnalysisQueue::JobId> AnalysisQueue::submit (Request request, Callback onDone, std::string* error)
{
    auto refuse = [error] (const std::string& why) -> std::optional<JobId> {
        if (error != nullptr)
            *error = why;
        return std::nullopt;
    };
    if (request.audio == nullptr || request.audio->isEmpty())
        return refuse ("no audio to analyse");
    if (request.source.contentHash.empty())
        return refuse ("the recording has no content hash (an asset must be identified)");
    const auto key = keyOf (request.source, request.settings);
    const std::lock_guard<std::mutex> lock (mutex);
    if (stopping)
        return refuse ("the analysis queue is shutting down");
    const auto id = nextId++;
    // The same recording with the same settings already queued or running: share it.
    if (auto it = byKey.find (key); it != byKey.end() && ! it->second->cancel)
    {
        it->second->waiting.emplace_back (id, std::move (onDone));
        byId[id] = it->second;
        return id;
    }
    const double seconds = std::min (request.audio->durationSeconds(), request.settings.maxAnalysisSeconds);
    if (queuedSecondsLocked() + seconds > maxSeconds)
        return refuse ("the analysis queue is full (" + std::to_string (static_cast<int> (maxSeconds)) + " s of audio waiting): try again shortly");
    auto job = std::make_shared<Job>();
    job->key = key;
    job->request = std::move (request);
    job->waiting.emplace_back (id, std::move (onDone));
    byKey[key] = job;
    byId[id] = job;
    pending.push_back (std::move (job));
    wake.notify_one();
    return id;
}

void AnalysisQueue::cancel (JobId id)
{
    const std::lock_guard<std::mutex> lock (mutex);
    auto it = byId.find (id);
    if (it == byId.end())
        return;
    auto job = it->second;
    byId.erase (it);
    finished[id] = State::cancelled;
    job->waiting.erase (std::remove_if (job->waiting.begin(), job->waiting.end(), [id] (const auto& w) { return w.first == id; }),
                        job->waiting.end());
    if (! job->waiting.empty())
        return;   // other requests still want this analysis
    job->cancel = true;
    byKey.erase (job->key);
    if (! job->running)
    {
        pending.erase (std::remove (pending.begin(), pending.end(), job), pending.end());
        if (pending.empty() && busy == 0)
            idle.notify_all();
    }
}

AnalysisQueue::Status AnalysisQueue::status (JobId id) const
{
    const std::lock_guard<std::mutex> lock (mutex);
    if (auto it = byId.find (id); it != byId.end())
        return { it->second->running ? State::running : State::queued, it->second->progress.load (std::memory_order_relaxed) };
    if (auto it = finished.find (id); it != finished.end())
        return { it->second, it->second == State::finished ? 1.0f : 0.0f };
    return {};
}

void AnalysisQueue::waitUntilIdle()
{
    std::unique_lock<std::mutex> lock (mutex);
    idle.wait (lock, [this] { return pending.empty() && busy == 0; });
}

void AnalysisQueue::run()
{
    for (;;)
    {
        std::shared_ptr<Job> job;
        {
            std::unique_lock<std::mutex> lock (mutex);
            wake.wait (lock, [this] { return stopping || ! pending.empty(); });
            if (stopping)
                return;
            job = pending.front();
            pending.pop_front();
            job->running = true;
            ++busy;
        }
        const auto& r = job->request;
        auto result = analysePhrase (*r.audio, r.source, r.settings, &job->cancel, &job->progress);
        std::vector<std::pair<JobId, Callback>> waiting;
        {
            const std::lock_guard<std::mutex> lock (mutex);
            if (auto it = byKey.find (job->key); it != byKey.end() && it->second == job)
                byKey.erase (it);
            const bool cancelled = result.cancelled || job->cancel;
            waiting = std::move (job->waiting);
            job->waiting.clear();
            for (const auto& [id, callback] : waiting)
            {
                byId.erase (id);
                finished[id] = cancelled ? State::cancelled : (result.ok ? State::finished : State::failed);
            }
            if (cancelled)
                waiting.clear();
            // Keep the record of finished requests bounded.
            while (finished.size() > 4096)
                finished.erase (finished.begin());
        }
        for (const auto& [id, callback] : waiting)
            if (callback)
                callback (id, result);
        {
            const std::lock_guard<std::mutex> lock (mutex);
            --busy;
            if (pending.empty() && busy == 0)
                idle.notify_all();
        }
    }
}

} // namespace osp::phrase
