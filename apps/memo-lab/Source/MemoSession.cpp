#include "MemoSession.h"

#include "core/PitchMath.h"

#include <cmath>

namespace osp::memolab
{

namespace
{
    juce::String seconds (std::int64_t frames, double rate)
    {
        return juce::String (static_cast<double> (frames) / rate, 2);
    }

    juce::String noteName (double midi)
    {
        return juce::String (midiNoteName (static_cast<int> (std::lround (midi))));
    }
}

MemoSession::MemoSession (OspAudioProcessor& p) : osp (p)
{
    // Passages from an earlier run are not needed (OSP's sample store holds what it uses).
    passageFolder().deleteRecursively();
}

MemoSession::~MemoSession()
{
    passageFolder().deleteRecursively();
}

juce::File MemoSession::writePassage (const DecodedAudio& audio, juce::Range<std::int64_t> frames, const juce::String& title, const juce::File& folder,
                                      juce::String& error)
{
    const auto total = static_cast<std::int64_t> (audio.buffer.getNumSamples());
    if (frames.isEmpty())
        frames = { 0, total };
    frames = frames.getIntersectionWith ({ 0, total });
    if (frames.getLength() < static_cast<std::int64_t> (0.05 * audio.info.sampleRate))
    {
        error = "The selection is too short to play (choose at least a twentieth of a second).";
        return {};
    }
    folder.deleteRecursively();   // one conversion at a time: the previous passage is done
    if (! folder.createDirectory())
    {
        error = "Memo Lab cannot write its working folder (" + folder.getFullPathName() + ").";
        return {};
    }
    auto base = juce::File::createLegalFileName (title.trim());
    if (base.isEmpty())
        base = "Recording";
    if (frames.getStart() > 0 || frames.getEnd() < total)
        base << " " << seconds (frames.getStart(), audio.info.sampleRate) << "-" << seconds (frames.getEnd(), audio.info.sampleRate) << " s";
    const auto file = folder.getChildFile (base + ".wav");
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate (audio.info.sampleRate)
                             .withNumChannels (audio.buffer.getNumChannels())
                             .withBitsPerSample (32)
                             .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
    std::unique_ptr<juce::AudioFormatWriter> writer;
    if (stream != nullptr)
        writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
    {
        error = "The passage could not be written.";
        return {};
    }
    if (! writer->writeFromAudioSampleBuffer (audio.buffer, static_cast<int> (frames.getStart()), static_cast<int> (frames.getLength())))
    {
        error = "The passage could not be written (the disk may be full).";
        return {};
    }
    writer.reset();
    return file;
}

bool MemoSession::convert (const Recording& recording, const DecodedAudio& audio, juce::Range<std::int64_t> frames, Mode m, juce::String& error)
{
    if (busy())
    {
        error = "An instrument is still being made. Wait for it to finish.";
        return false;
    }
    const auto file = writePassage (audio, frames, recording.title, passageFolder(), error);
    if (file == juce::File())
        return false;
    mode = m;
    source = recording.title;
    name = recording.title + (m == Mode::mapNotes ? " (notes)" : "");
    const auto before = osp.currentInstrument (0);
    previousLoad = before != nullptr ? before->loadId : 0;
    started = true;
    sawWork = false;
    quietPolls = 0;
    stageAQuietSince = 0;
    current = {};
    current.mode = m;
    current.stage = Status::Stage::building;
    current.text = m == Mode::mapNotes ? "Finding the notes" : "Making the instrument";
    osp.keyboardState.allNotesOff (0);
    if (m == Mode::mapNotes)
        osp.analyseRecording (file, 0);
    else
        osp.loadFile (file, 0);
    return true;
}

bool MemoSession::busy() const
{
    return ! osp.waitForLoads (0) || osp.atlasActivity (0) != InstrumentHost::AtlasActivity::none;
}

MemoSession::Status MemoSession::update()
{
    if (! started)
        return current;
    const auto activity = osp.atlasActivity (0);
    const bool working = busy();
    sawWork = sawWork || working;
    const auto instrument = osp.currentInstrument (0);
    const bool fresh = instrument != nullptr && instrument->loadId != previousLoad;
    current.atlas = fresh && instrument->atlas != nullptr;
    if (current.atlas)
    {
        double lo = 1.0e9, hi = -1.0e9;
        int count = 0;
        for (const auto& n : instrument->atlas->notes)
            if (n.included)
            {
                ++count;
                lo = std::min (lo, static_cast<double> (n.playKey()));
                hi = std::max (hi, static_cast<double> (n.playKey()));
            }
        current.notes = count;
        current.pitchRange = count > 0 ? (noteName (lo) + juce::String::fromUTF8 (" \xe2\x80\x93 ") + noteName (hi)) : juce::String();
    }
    else if (fresh)
    {
        current.notes = 0;
        current.pitchRange = noteName (osp.effectiveRootMidi (0));
    }

    using Stage = Status::Stage;
    if (working)
    {
        stageAQuietSince = 0;
        const bool playable = fresh && (mode == Mode::oneShot || current.atlas);
        current.stage = playable ? Stage::playable : Stage::building;
        if (activity == InstrumentHost::AtlasActivity::analysing)
            current.text = "Finding the notes";
        else if (activity == InstrumentHost::AtlasActivity::preparing)
            current.text = "Preparing the notes";
        else if (activity == InstrumentHost::AtlasActivity::enhancing)
            current.text = "Playable. Adding sustain and release";
        else
            current.text = playable ? "Playable" : "Making the instrument";
        return current;
    }
    // Idle: a load may finish between two polls before ever being seen as busy, so a fresh
    // instrument is the outcome; with none after a few quiet polls, the load failed.
    if (! fresh)
    {
        if (++quietPolls < 3 && ! sawWork)
            return current;
        current.stage = Stage::failed;
        const auto why = osp.statusMessage();
        current.text = "The instrument could not be made" + (why.isNotEmpty() ? ": " + why : juce::String (".")) + " The recording is unchanged.";
        started = false;
        return current;
    }
    // Stage A's notes with nothing running: Stage B's loader may have finished while its
    // instrument is not published yet (the next pollLoads does it). Stay "Playable" until
    // the full instrument is there; a Stage B that never comes (it failed) ends it after a
    // few quiet seconds, the notes still playable.
    if (mode == Mode::mapNotes && current.atlas && instrument->atlasStatus != nullptr && instrument->atlasStatus->playableOnly)
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (stageAQuietSince == 0)
            stageAQuietSince = now;
        if (now - stageAQuietSince < 3000)
        {
            current.stage = Stage::playable;
            current.text = "Playable. Adding sustain and release";
            return current;
        }
    }
    stageAQuietSince = 0;
    osp.dismissMapNotesPrompt (0);
    if (mode == Mode::mapNotes && ! current.atlas)
    {
        current.stage = Stage::unsuitable;
        const auto why = osp.statusMessage();
        current.text = "Not suitable for note mapping" + (why.isNotEmpty() ? " (" + why + ")" : juce::String()) + ". It plays as one sample instead (ONE SHOT).";
    }
    else
    {
        current.stage = Stage::ready;
        current.text = current.atlas ? "Ready: " + juce::String (current.notes) + (current.notes == 1 ? " note, " : " notes, ") + current.pitchRange
                                     : "Ready: one sample, root " + current.pitchRange;
    }
    started = false;
    return current;
}

juce::String MemoSession::suggestedFileName() const
{
    auto base = juce::File::createLegalFileName (name.trim());
    return (base.isEmpty() ? juce::String ("Memo instrument") : base) + OspAudioProcessor::instrumentExtension;
}

bool MemoSession::save (const juce::File& file, juce::String& error)
{
    if (! current.canPlay() || osp.currentInstrument (0) == nullptr)
    {
        error = "There is no instrument to save yet.";
        return false;
    }
    if (busy())
    {
        error = "The instrument is still being prepared. Save it when it is ready.";
        return false;
    }
    OspAudioProcessor::ExportOptions options;
    options.name = name;
    options.notes = "Made with ANDOR Memo Lab from \"" + source + "\".";
    options.tags = { "memo lab" };
    const auto target = file.hasFileExtension (OspAudioProcessor::instrumentExtension) ? file : file.withFileExtension (OspAudioProcessor::instrumentExtension);
    return osp.exportInstrument (target, error, options);
}

//==============================================================================
PreviewPlayer::PreviewPlayer()
{
    player.setSource (&transport);
}

PreviewPlayer::~PreviewPlayer()
{
    player.setSource (nullptr);
    transport.setSource (nullptr);
}

void PreviewPlayer::setAudio (std::shared_ptr<const DecodedAudio> a)
{
    stop();
    transport.setSource (nullptr);
    memory.reset();
    view.reset();
    audio = std::move (a);
}

void PreviewPlayer::play (juce::Range<std::int64_t> frames, std::int64_t from)
{
    if (audio == nullptr)
        return;
    const auto total = static_cast<std::int64_t> (audio->buffer.getNumSamples());
    if (frames.isEmpty())
        frames = { std::clamp<std::int64_t> (from, 0, total), total };
    frames = frames.getIntersectionWith ({ 0, total });
    if (frames.getLength() <= 0)
        return;
    transport.setSource (nullptr);
    // A view of the passage (no copy): the source ends where the passage ends.
    view = std::make_unique<juce::AudioBuffer<float>> (const_cast<float* const*> (audio->buffer.getArrayOfReadPointers()), audio->buffer.getNumChannels(),
                                                       static_cast<int> (frames.getStart()), static_cast<int> (frames.getLength()));
    memory = std::make_unique<juce::MemoryAudioSource> (*view, false, false);
    offset = frames.getStart();
    transport.setSource (memory.get(), 0, nullptr, audio->info.sampleRate, std::max (2, audio->buffer.getNumChannels()));
    transport.setPosition (0.0);
    transport.start();
}

void PreviewPlayer::stop()
{
    transport.stop();
}

bool PreviewPlayer::isPlaying() const
{
    return transport.isPlaying();
}

std::int64_t PreviewPlayer::position() const
{
    if (audio == nullptr || ! transport.isPlaying())
        return -1;
    return offset + static_cast<std::int64_t> (transport.getCurrentPosition() * audio->info.sampleRate);
}

} // namespace osp::memolab
