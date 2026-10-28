#include "andor/phrase/PhraseFixtures.h"

#include "core/PitchMath.h"
#include "core/Prng.h"

#include <algorithm>
#include <cmath>

namespace osp::phrase
{

namespace
{
    constexpr double twoPi = 6.283185307179586;

    struct NoteSpec
    {
        double start = 0.0;
        double duration = 0.4;
        double midi = 60.0;
        double level = 1.0;            ///< linear, relative to the phrase level
        bool legatoToNext = false;     ///< no gap and no new attack: the pitch glides
        double dipDb = 0.0;            ///< legato only: a level dip at the join (re-articulation)
        double vibratoCents = 0.0;
        double vibratoHz = 5.5;
        double scoopCents = 0.0;       ///< starts this far below and glides up (singers' scoop)
        double scoopSeconds = 0.08;    ///< ... over this long
    };

    struct Style
    {
        double fundamentalDb = 0.0;    ///< the first harmonic's level against the tilt (octave ambiguity: -18)
        int harmonics = 24;
        double noiseSnrDb = 200.0;     ///< white noise against the voiced level
        bool breath = false;           ///< noise bursts in the gaps between notes
        double attackSeconds = 0.025;
        double releaseSeconds = 0.06;
        double glideSeconds = 0.05;
        double phraseLevel = 0.3;      ///< peak-ish amplitude of a level-1 note
        double tremoloDb = 0.0;        ///< amplitude tremolo depth (peak to trough)
        double tremoloHz = 6.0;
        double breathinessDb = 200.0;  ///< noise inside the tone, following its envelope, below the tone
        double swellDb = 0.0;          ///< irregular breath/bow swells (peak to trough), not periodic like tremolo
    };

    /** A vowel-like spectral envelope ("ah": 700 / 1200 / 2600 Hz) so the tone has formants. */
    double formantGain (double hz)
    {
        auto peak = [&] (double centre, double width, double gain) { return gain / std::sqrt (1.0 + std::pow ((hz - centre) / width, 2.0)); };
        return 0.08 + peak (700.0, 130.0, 1.0) + peak (1200.0, 150.0, 0.6) + peak (2600.0, 250.0, 0.25);
    }

    AudioData render (const std::vector<NoteSpec>& notes, const Style& style, double sampleRate, double totalSeconds, std::uint64_t seed)
    {
        const auto frames = static_cast<std::size_t> (totalSeconds * sampleRate);
        std::vector<double> amp (frames, 0.0), midi (frames, notes.empty() ? 60.0 : notes.front().midi), vib (frames, 0.0);

        for (std::size_t n = 0; n < notes.size(); ++n)
        {
            const auto& note = notes[n];
            const bool joinedIn = n > 0 && notes[n - 1].legatoToNext;
            const bool joinedOut = note.legatoToNext && n + 1 < notes.size();
            const auto begin = static_cast<std::size_t> (note.start * sampleRate);
            const double endSeconds = note.start + note.duration;
            const auto end = std::min (frames, static_cast<std::size_t> ((endSeconds + (joinedOut ? 0.0 : style.releaseSeconds)) * sampleRate));
            for (auto i = begin; i < end; ++i)
            {
                const double t = static_cast<double> (i) / sampleRate - note.start;
                double e = 1.0;
                if (! joinedIn)
                    e = std::min (e, t / style.attackSeconds);
                if (! joinedOut && t > note.duration)
                    e = std::min (e, 1.0 - (t - note.duration) / style.releaseSeconds);
                e = std::clamp (e, 0.0, 1.0);
                amp[i] = std::max (amp[i], e * note.level);
                if (t <= note.duration)
                {
                    double m = note.midi;
                    if (note.scoopCents != 0.0 && t < note.scoopSeconds)
                        m -= note.scoopCents / 100.0 * (1.0 - t / note.scoopSeconds);
                    midi[i] = m;
                    const double depth = note.vibratoCents / 100.0 * std::clamp (t / 0.15, 0.0, 1.0);
                    vib[i] = depth * std::sin (twoPi * note.vibratoHz * t);
                }
            }
            // Hold the pitch through the release (the tone keeps its pitch while it fades).
            for (auto i = static_cast<std::size_t> (endSeconds * sampleRate); i < end; ++i)
                midi[i] = note.midi;
        }

        // Legato joins: glide across the boundary, and the optional re-articulation dip.
        for (std::size_t n = 0; n + 1 < notes.size(); ++n)
        {
            if (! notes[n].legatoToNext)
                continue;
            const double boundary = notes[n + 1].start;
            const double half = 0.5 * style.glideSeconds;
            for (auto i = static_cast<std::size_t> (std::max (0.0, boundary - half) * sampleRate);
                 i < std::min (frames, static_cast<std::size_t> ((boundary + half) * sampleRate)); ++i)
            {
                const double x = (static_cast<double> (i) / sampleRate - (boundary - half)) / style.glideSeconds;
                const double smooth = 0.5 - 0.5 * std::cos (3.141592653589793 * std::clamp (x, 0.0, 1.0));
                midi[i] = notes[n].midi + (notes[n + 1].midi - notes[n].midi) * smooth;
            }
            if (notes[n].dipDb > 0.0)
            {
                const double width = 0.06;
                for (auto i = static_cast<std::size_t> (std::max (0.0, boundary - width) * sampleRate);
                     i < std::min (frames, static_cast<std::size_t> ((boundary + width) * sampleRate)); ++i)
                {
                    const double x = (static_cast<double> (i) / sampleRate - boundary) / width;
                    const double shape = 0.5 + 0.5 * std::cos (3.141592653589793 * std::clamp (x, -1.0, 1.0));
                    amp[i] *= dbToGain (-notes[n].dipDb * shape);
                }
            }
        }

        AudioData audio = AudioData::allocate (1, static_cast<std::int64_t> (frames), sampleRate);
        auto& out = audio.channels[0];
        double phase = 0.0;
        double voicedPower = 0.0;
        std::size_t voicedCount = 0;
        for (std::size_t i = 0; i < frames; ++i)
        {
            const double f0 = midiToHz (midi[i] + vib[i]);
            phase += twoPi * f0 / sampleRate;
            if (phase > twoPi * 1024.0)
                phase -= twoPi * 1024.0;
            if (amp[i] <= 0.0)
                continue;
            double sum = 0.0, norm = 0.0;
            for (int k = 1; k <= style.harmonics; ++k)
            {
                const double hz = k * f0;
                if (hz > 0.45 * sampleRate)
                    break;
                double a = formantGain (hz) / k;
                if (k == 1)
                    a *= dbToGain (style.fundamentalDb);
                sum += a * std::sin (k * phase);
                norm += a;
            }
            double level = amp[i];
            if (style.tremoloDb > 0.0)
                level *= dbToGain (-0.5 * style.tremoloDb * (1.0 + std::sin (twoPi * style.tremoloHz * static_cast<double> (i) / sampleRate)));
            if (style.swellDb > 0.0)
            {
                // Two incommensurate slow cycles: swells and sags at uneven spacing (1-4 per second).
                const double t = static_cast<double> (i) / sampleRate;
                const double w = (std::sin (twoPi * 1.7 * t) + 0.7 * std::sin (twoPi * 3.3 * t + 1.3)) / 1.7;
                level *= dbToGain (-0.5 * style.swellDb * (1.0 - w));
            }
            const double s = norm > 0.0 ? level * style.phraseLevel * sum / norm * 2.0 : 0.0;
            out[i] = static_cast<float> (s);
            voicedPower += s * s;
            ++voicedCount;
        }

        Prng rng (seed);
        const double voicedRms = voicedCount > 0 ? std::sqrt (voicedPower / static_cast<double> (voicedCount)) : 0.0;
        if (style.noiseSnrDb < 150.0)
        {
            const double noiseRms = voicedRms * dbToGain (-style.noiseSnrDb);
            for (auto& s : out)
                s += static_cast<float> (noiseRms * rng.gaussian());
        }
        if (style.breathinessDb < 150.0)
        {
            // Aspiration: noise that follows the tone's own envelope (breathy voice, flute air).
            const double g = style.phraseLevel * dbToGain (-style.breathinessDb);
            for (std::size_t i = 0; i < frames; ++i)
                out[i] += static_cast<float> (g * amp[i] * rng.gaussian());
        }
        if (style.breath)
        {
            // Breath: band-limited noise (a crude high-pass + low-pass) in each gap of >= 0.2 s.
            for (std::size_t n = 0; n + 1 < notes.size(); ++n)
            {
                const double gapStart = notes[n].start + notes[n].duration + style.releaseSeconds + 0.02;
                const double gapEnd = notes[n + 1].start - 0.02;
                if (gapEnd - gapStart < 0.18)
                    continue;
                double lp = 0.0, hpPrev = 0.0, hpOut = 0.0;
                for (auto i = static_cast<std::size_t> (gapStart * sampleRate); i < std::min (frames, static_cast<std::size_t> (gapEnd * sampleRate)); ++i)
                {
                    const double x = (static_cast<double> (i) / sampleRate - gapStart) / (gapEnd - gapStart);
                    const double env = std::sin (3.141592653589793 * x);
                    const double white = rng.gaussian();
                    hpOut = 0.95 * (hpOut + white - hpPrev);
                    hpPrev = white;
                    lp += 0.35 * (hpOut - lp);
                    out[i] += static_cast<float> (voicedRms * dbToGain (-14.0) * env * lp);
                }
            }
        }
        return audio;
    }

    std::vector<TruthNote> truthOf (const std::vector<NoteSpec>& notes)
    {
        std::vector<TruthNote> truth;
        for (const auto& n : notes)
            truth.push_back ({ n.start, n.start + n.duration, n.midi });
        return truth;
    }

    /** Notes one after another with `gap` seconds between them. */
    std::vector<NoteSpec> sequence (const std::vector<double>& midis, double duration, double gap, double start = 0.2)
    {
        std::vector<NoteSpec> notes;
        double t = start;
        for (double m : midis)
        {
            NoteSpec n;
            n.start = t;
            n.duration = duration;
            n.midi = m;
            notes.push_back (n);
            t += duration + gap;
        }
        return notes;
    }

    double endOf (const std::vector<NoteSpec>& notes) { return notes.empty() ? 1.0 : notes.back().start + notes.back().duration + 0.5; }
}

std::vector<std::string> phraseStressFixtureNames()
{
    return { "stress-fast-run", "stress-wide-vibrato", "stress-portamento", "stress-bass", "stress-high",
             "stress-very-noisy", "stress-tremolo", "stress-breathy", "stress-breath-swells", "stress-scoops" };
}

std::vector<std::string> phraseFixtureNames()
{
    return { "clean-scale", "noisy", "vibrato", "legato", "long-sustain", "staccato", "silent-gaps",
             "repeated", "out-of-tune", "octave-ambiguity", "breath", "quiet-notes", "vocal-phrase" };
}

PhraseFixture makePhraseFixture (const std::string& name, double sr)
{
    PhraseFixture fx;
    fx.name = name;
    Style style;
    std::vector<NoteSpec> notes;
    std::uint64_t seed = 1;

    if (name == "clean-scale")
    {
        fx.description = "C major scale C4..C5, 0.4 s notes, 0.1 s gaps";
        notes = sequence ({ 60, 62, 64, 65, 67, 69, 71, 72 }, 0.4, 0.1);
    }
    else if (name == "noisy")
    {
        fx.description = "melody with white noise at 15 dB SNR";
        notes = sequence ({ 57, 60, 64, 62, 59, 57 }, 0.45, 0.12);
        style.noiseSnrDb = 15.0;
        seed = 2;
    }
    else if (name == "vibrato")
    {
        fx.description = "four long notes with vibrato (60 cents at 5.5 Hz; the last 100 cents at 6 Hz)";
        notes = sequence ({ 64, 67, 62, 65 }, 1.2, 0.2);
        for (auto& n : notes)
            n.vibratoCents = 60.0;
        notes.back().vibratoCents = 100.0;
        notes.back().vibratoHz = 6.0;
    }
    else if (name == "legato")
    {
        fx.description = "six legato notes, no gaps, 50 ms glides (steps of 2 to 5 semitones)";
        notes = sequence ({ 62, 64, 67, 65, 60, 62 }, 0.5, 0.0);
        for (std::size_t i = 0; i + 1 < notes.size(); ++i)
            notes[i].legatoToNext = true;
    }
    else if (name == "long-sustain")
    {
        fx.description = "one 6 s note with light vibrato (30 cents)";
        notes = sequence ({ 55 }, 6.0, 0.0);
        notes[0].vibratoCents = 30.0;
        notes[0].vibratoHz = 5.0;
    }
    else if (name == "staccato")
    {
        fx.description = "twelve short notes (90 ms) with 110 ms gaps";
        notes = sequence ({ 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 64, 67 }, 0.09, 0.11);
        style.attackSeconds = 0.008;
        style.releaseSeconds = 0.03;
    }
    else if (name == "silent-gaps")
    {
        fx.description = "five notes separated by 1 s of silence";
        notes = sequence ({ 60, 63, 58, 65, 61 }, 0.5, 1.0);
    }
    else if (name == "repeated")
    {
        fx.description = "G3 four times with gaps, then four times legato with 12 dB re-articulation dips";
        notes = sequence ({ 55, 55, 55, 55 }, 0.35, 0.15);
        auto legato = sequence ({ 55, 55, 55, 55 }, 0.4, 0.0, notes.back().start + 0.8);
        for (std::size_t i = 0; i + 1 < legato.size(); ++i)
        {
            legato[i].legatoToNext = true;
            legato[i].dipDb = 12.0;
        }
        notes.insert (notes.end(), legato.begin(), legato.end());
    }
    else if (name == "out-of-tune")
    {
        fx.description = "notes off their keys: +30, -40, +45, -20, +10 cents";
        notes = sequence ({ 60.30, 62.0 - 0.40, 64.45, 65.0 - 0.20, 67.10 }, 0.5, 0.15);
    }
    else if (name == "octave-ambiguity")
    {
        fx.description = "low notes (A2..A3) with the fundamental 18 dB under the second harmonic's tilt";
        notes = sequence ({ 45, 48, 52, 50, 57 }, 0.6, 0.15);
        style.fundamentalDb = -18.0;
    }
    else if (name == "breath")
    {
        fx.description = "notes with breath noise in the 0.35 s gaps";
        notes = sequence ({ 64, 62, 60, 62, 64 }, 0.55, 0.45);
        style.breath = true;
        seed = 3;
    }
    else if (name == "quiet-notes")
    {
        fx.description = "loud and -24 dB notes alternating";
        notes = sequence ({ 60, 64, 67, 64, 60, 62 }, 0.45, 0.12);
        for (std::size_t i = 1; i < notes.size(); i += 2)
            notes[i].level = dbToGain (-24.0);
    }
    else if (name == "vocal-phrase")
    {
        fx.description = "a sung-style phrase: scoops, vibrato on long notes, legato pairs, breath between phrases";
        notes = {
            { 0.20, 0.35, 62, 1.0, false, 0.0, 0.0, 5.5, 60.0 },
            { 0.62, 0.30, 64, 1.0, true, 0.0, 0.0, 5.5, 0.0 },
            { 0.92, 0.90, 66, 0.9, false, 0.0, 55.0, 5.8, 40.0 },
            { 2.30, 0.28, 69, 0.8, true, 0.0, 0.0, 5.5, 0.0 },
            { 2.58, 0.28, 67, 0.8, true, 0.0, 0.0, 5.5, 0.0 },
            { 2.86, 1.10, 66, 0.9, false, 0.0, 70.0, 5.6, 0.0 },
            { 4.50, 0.40, 62, 0.7, false, 0.0, 0.0, 5.5, 80.0 },
            { 5.00, 1.40, 64, 0.85, false, 0.0, 60.0, 5.4, 0.0 },
        };
        style.breath = true;
        style.noiseSnrDb = 35.0;
        seed = 4;
    }
    else if (name == "stress-fast-run")
    {
        fx.description = "legato run of sixteenths at 140 bpm (107 ms notes), steps of 1-2 semitones";
        notes = sequence ({ 60, 62, 64, 65, 67, 65, 64, 62, 60, 59, 60, 62 }, 0.107, 0.0);
        for (std::size_t i = 0; i + 1 < notes.size(); ++i)
            notes[i].legatoToNext = true;
        style.glideSeconds = 0.025;
    }
    else if (name == "stress-wide-vibrato")
    {
        fx.description = "slow, wide vibrato (150 cents at 4.5 Hz) on three long notes";
        notes = sequence ({ 64, 60, 67 }, 1.4, 0.25);
        for (auto& n : notes)
        {
            n.vibratoCents = 150.0;
            n.vibratoHz = 4.5;
        }
    }
    else if (name == "stress-portamento")
    {
        fx.description = "legato notes joined by slow 250 ms slides";
        notes = sequence ({ 57, 62, 59, 64 }, 0.7, 0.0);
        for (std::size_t i = 0; i + 1 < notes.size(); ++i)
            notes[i].legatoToNext = true;
        style.glideSeconds = 0.25;
    }
    else if (name == "stress-bass")
    {
        fx.description = "low register E1..E2 (41-82 Hz)";
        notes = sequence ({ 28, 31, 33, 35, 40 }, 0.6, 0.15);
    }
    else if (name == "stress-high")
    {
        fx.description = "high register C6..C7 (1047-2093 Hz), above the default 1500 Hz search range at the top";
        notes = sequence ({ 84, 88, 91, 93, 96 }, 0.4, 0.12);
    }
    else if (name == "stress-very-noisy")
    {
        fx.description = "melody at 6 dB SNR";
        notes = sequence ({ 62, 65, 69, 67, 64 }, 0.5, 0.15);
        style.noiseSnrDb = 6.0;
        seed = 5;
    }
    else if (name == "stress-tremolo")
    {
        fx.description = "legato notes with a deep 9 dB amplitude tremolo at 6 Hz (must not split)";
        notes = sequence ({ 60, 63, 67 }, 1.0, 0.0);
        for (std::size_t i = 0; i + 1 < notes.size(); ++i)
            notes[i].legatoToNext = true;
        style.tremoloDb = 9.0;
    }
    else if (name == "stress-breathy")
    {
        fx.description = "breathy tone: aspiration noise 8 dB under the tone, following its envelope";
        notes = sequence ({ 67, 69, 71, 74, 72 }, 0.5, 0.15);
        style.breathinessDb = 8.0;
        seed = 6;
    }
    else if (name == "stress-breath-swells")
    {
        // Round 3 (from a real ney recording): breath swells sag 8-11 dB without a new attack.
        fx.description = "held breathy notes, legato pairs, with irregular 11 dB breath swells (must not split)";
        notes = sequence ({ 69, 76, 81, 76, 78, 74 }, 1.5, 0.3);
        notes[0].legatoToNext = notes[2].legatoToNext = notes[4].legatoToNext = true;
        notes[1].start = notes[0].start + notes[0].duration;
        notes[3].start = notes[2].start + notes[2].duration;
        notes[5].start = notes[4].start + notes[4].duration;
        style.swellDb = 11.0;
        style.breathinessDb = 14.0;
        seed = 7;
    }
    else if (name == "stress-scoops")
    {
        // Round 3: a phrase that starts with a slow slide up into the note (ney, voice, bends).
        fx.description = "notes after silence that each scoop up 1-2 semitones over 160 ms (the scoop is the attack)";
        notes = sequence ({ 66, 78, 71, 74, 69 }, 0.9, 0.35);
        const double scoops[] = { 200.0, 150.0, 100.0, 200.0, 170.0 };
        for (std::size_t i = 0; i < notes.size(); ++i)
        {
            notes[i].scoopCents = scoops[i];
            notes[i].scoopSeconds = 0.16;
        }
    }
    else
    {
        fx.description = "unknown fixture";
        return fx;
    }

    fx.audio = render (notes, style, sr, endOf (notes), seed);
    fx.notes = truthOf (notes);
    return fx;
}

//==============================================================================
EvaluationResult evaluate (const atlas::NoteAtlas& atlas, const std::vector<TruthNote>& truth, const EvaluationSettings& settings)
{
    EvaluationResult r;
    r.truthNotes = static_cast<int> (truth.size());
    struct Detected
    {
        double onset, offset, midi;
        bool used = false;
    };
    std::vector<Detected> detected;
    const double sr = atlas.sources.empty() ? 48000.0 : atlas.sources.front().sampleRate;
    for (const auto& n : atlas.notes)
        if (n.included)
            detected.push_back ({ static_cast<double> (n.attackStart) / sr, static_cast<double> (n.sourceEnd) / sr, n.midiPitch });
    r.detectedNotes = static_cast<int> (detected.size());

    double onsetSum = 0.0, offsetSum = 0.0, pitchSum = 0.0;
    for (const auto& t : truth)
    {
        int best = -1;
        double bestDistance = settings.onsetToleranceSeconds;
        for (std::size_t d = 0; d < detected.size(); ++d)
        {
            const double distance = std::abs (detected[d].onset - t.startSeconds);
            if (! detected[d].used && distance <= bestDistance)
            {
                best = static_cast<int> (d);
                bestDistance = distance;
            }
        }
        if (best < 0)
        {
            ++r.missed;
            continue;
        }
        auto& d = detected[static_cast<std::size_t> (best)];
        d.used = true;
        const double cents = (d.midi - t.midi) * 100.0;
        if (std::abs (cents) <= settings.pitchToleranceCents)
        {
            ++r.matched;
            onsetSum += std::abs (d.onset - t.startSeconds);
            offsetSum += std::abs (d.offset - t.endSeconds);
            pitchSum += std::abs (cents);
        }
        else if (std::abs (std::abs (cents) - 1200.0) <= settings.pitchToleranceCents)
            ++r.octaveErrors;
        else
            ++r.pitchErrors;
    }
    for (const auto& d : detected)
    {
        if (d.used)
            continue;
        bool inside = false;
        for (const auto& t : truth)
            inside = inside || (d.onset > t.startSeconds && d.onset < t.endSeconds);
        if (inside)
            ++r.falseSplits;
        else
            ++r.spurious;
    }
    if (r.matched > 0)
    {
        r.meanOnsetErrorMs = 1000.0 * onsetSum / r.matched;
        r.meanOffsetErrorMs = 1000.0 * offsetSum / r.matched;
        r.meanPitchErrorCents = pitchSum / r.matched;
    }
    return r;
}

} // namespace osp::phrase
