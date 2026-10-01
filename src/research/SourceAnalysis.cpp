#include "research/SourceAnalysis.h"

#include "analysis/Analyzer.h"
#include "io/AudioFileIO.h"
#include "io/ContentHash.h"

#include <exception>

namespace osp::research
{

AnalysedSource loadAndAnalyse (const std::filesystem::path& path, const AnalysisOptions& options)
{
    AnalysedSource result;
    try
    {
        auto loaded = io::loadAudioFile (path);
        if (! loaded.ok)
        {
            result.error = loaded.error;
            return result;
        }

        result.analysis = Analyzer::analyse (loaded.audio, options);

        auto& src = result.analysis.source;
        src.filename = path.filename().string();
        src.path = path.string();
        if (const auto hash = io::sha256OfFile (path))
            src.contentHash = io::contentId (*hash);
        src.format = loaded.info.formatName;
        src.bitDepth = loaded.info.bitDepth;
        src.isFloatingPoint = loaded.info.isFloatingPoint;
        src.channels = loaded.info.channels;
        src.truncated = loaded.truncated;

        // Loader warnings first: they explain everything after them.
        result.analysis.warnings.insert (result.analysis.warnings.begin(), loaded.warnings.begin(), loaded.warnings.end());

        result.audio = std::move (loaded.audio);
        result.ok = true;
    }
    catch (const std::exception& e)
    {
        result.error = std::string ("unexpected error: ") + e.what();
    }
    catch (...)
    {
        result.error = "unexpected unknown error";
    }
    return result;
}

} // namespace osp::research
