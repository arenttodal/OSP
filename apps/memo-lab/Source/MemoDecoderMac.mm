// The AVFoundation reader for recordings CoreAudio's file reader will not open (Voice Memos'
// QuickTime-audio .qta and similar): the first audio track through AVAssetReader as 32-bit
// float PCM at its own rate, one or two channels. Read only; nothing is written.

#include "MemoDecoder.h"

#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace osp::memolab
{

namespace
{
    // (JUCE's own string bridges are private to its modules.)
    NSString* toNS (const juce::String& s) { return [NSString stringWithUTF8String: s.toRawUTF8()]; }
    juce::String fromNS (NSString* s) { return s != nil ? juce::String::fromUTF8 ([s UTF8String]) : juce::String(); }

    AVAssetTrack* firstAudioTrack (AVURLAsset* asset)
    {
        JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
        NSArray<AVAssetTrack*>* tracks = [asset tracksWithMediaType: AVMediaTypeAudio];
        JUCE_END_IGNORE_DEPRECATION_WARNINGS
        return tracks.count > 0 ? tracks.firstObject : nil;
    }

    bool trackFormat (AVAssetTrack* track, double& rate, int& channels)
    {
        JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
        NSArray* descriptions = track.formatDescriptions;
        JUCE_END_IGNORE_DEPRECATION_WARNINGS
        if (descriptions.count == 0)
            return false;
        auto description = (__bridge CMAudioFormatDescriptionRef) descriptions.firstObject;
        const AudioStreamBasicDescription* asbd = CMAudioFormatDescriptionGetStreamBasicDescription (description);
        if (asbd == nullptr)
            return false;
        rate = asbd->mSampleRate;
        channels = static_cast<int> (asbd->mChannelsPerFrame);
        return rate > 0.0 && channels > 0;
    }
}

bool probeWithAVFoundation (const juce::File& file, AudioInfo& info, juce::String& error)
{
    @autoreleasepool
    {
        NSURL* url = [NSURL fileURLWithPath: toNS (file.getFullPathName())];
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL: url options: nil];
        AVAssetTrack* track = firstAudioTrack (asset);
        double rate = 0.0;
        int channels = 0;
        if (track == nil || ! trackFormat (track, rate, channels))
        {
            error = "no audio track AVFoundation can read";
            return false;
        }
        JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
        const double seconds = CMTimeGetSeconds (asset.duration);
        JUCE_END_IGNORE_DEPRECATION_WARNINGS
        info.sampleRate = rate;
        info.channels = channels;
        info.frames = std::isfinite (seconds) ? static_cast<std::int64_t> (std::llround (seconds * rate)) : 0;
        info.format = "AVFoundation";
        return info.frames > 0;
    }
}

bool decodeWithAVFoundation (const juce::File& file, DecodedAudio& out, juce::String& error, double maxSeconds)
{
    @autoreleasepool
    {
        NSURL* url = [NSURL fileURLWithPath: toNS (file.getFullPathName())];
        AVURLAsset* asset = [AVURLAsset URLAssetWithURL: url options: nil];
        AVAssetTrack* track = firstAudioTrack (asset);
        double rate = 0.0;
        int sourceChannels = 0;
        if (track == nil || ! trackFormat (track, rate, sourceChannels))
        {
            error = "This recording's format cannot be read on this Mac.";
            return false;
        }
        const int channels = std::min (2, sourceChannels);
        NSError* failure = nil;
        AVAssetReader* reader = [AVAssetReader assetReaderWithAsset: asset error: &failure];
        if (reader == nil)
        {
            error = "This recording could not be opened (" + fromNS (failure.localizedDescription) + ").";
            return false;
        }
        NSDictionary* settings = @{
            AVFormatIDKey: @(kAudioFormatLinearPCM),
            AVLinearPCMBitDepthKey: @32,
            AVLinearPCMIsFloatKey: @YES,
            AVLinearPCMIsNonInterleaved: @NO,
            AVLinearPCMIsBigEndianKey: @NO,
            AVNumberOfChannelsKey: @(channels),
            AVSampleRateKey: @(rate)
        };
        AVAssetReaderTrackOutput* output = [AVAssetReaderTrackOutput assetReaderTrackOutputWithTrack: track outputSettings: settings];
        if (! [reader canAddOutput: output])
        {
            error = "This recording's audio cannot be decoded.";
            return false;
        }
        [reader addOutput: output];
        if (! [reader startReading])
        {
            error = "This recording could not be read (not downloaded, or damaged).";
            return false;
        }
        const auto limit = static_cast<std::size_t> (std::floor (maxSeconds * rate)) * static_cast<std::size_t> (channels);
        std::vector<float> interleaved;
        bool truncated = false;
        while (true)
        {
            CMSampleBufferRef sample = [output copyNextSampleBuffer];
            if (sample == nullptr)
                break;
            if (CMBlockBufferRef block = CMSampleBufferGetDataBuffer (sample))
            {
                const size_t bytes = CMBlockBufferGetDataLength (block);
                const size_t count = bytes / sizeof (float);
                const size_t start = interleaved.size();
                const size_t take = std::min (count, limit > start ? limit - start : 0);
                if (take < count)
                    truncated = true;
                interleaved.resize (start + take);
                if (take > 0)
                    CMBlockBufferCopyDataBytes (block, 0, take * sizeof (float), interleaved.data() + start);
            }
            CFRelease (sample);
            if (truncated)
            {
                [reader cancelReading];
                break;
            }
        }
        if (reader.status == AVAssetReaderStatusFailed)
        {
            error = "This recording could not be decoded (" + fromNS (reader.error.localizedDescription) + ").";
            return false;
        }
        const auto frames = static_cast<int> (interleaved.size() / static_cast<std::size_t> (channels));
        out.buffer.setSize (channels, frames);
        for (int c = 0; c < channels; ++c)
        {
            auto* dest = out.buffer.getWritePointer (c);
            for (int i = 0; i < frames; ++i)
                dest[i] = interleaved[static_cast<std::size_t> (i * channels + c)];
        }
        out.info.sampleRate = rate;
        out.info.channels = channels;
        out.info.frames = frames;
        out.info.format = "AVFoundation";
        out.truncated = truncated;
        return frames > 0;
    }
}

} // namespace osp::memolab
