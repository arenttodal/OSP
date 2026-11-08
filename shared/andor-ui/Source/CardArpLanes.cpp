#include "CardArpLanes.h"

#include "core/Prng.h"

#include <algorithm>

namespace osp::plugin
{

struct CardArpLanes::State
{
    std::array<Arpeggiator, 2> laneArps;     ///< cards B and C, unlinked
    juce::MidiBuffer laneMidi, legacyInput;
    std::array<std::array<std::uint8_t, 128>, 16> heldKeys {};      ///< velocity of each key down, by channel
    std::array<std::array<std::uint8_t, 128>, 16> passChannel {};   ///< the channel a held key sounds on (0: none)
    std::array<std::array<bool, 128>, 16> passSustained {};         ///< released under the pedal
    std::array<std::array<bool, 128>, 16> soundingOut {};           ///< what the engine was given, by channel
};

CardArpLanes::CardArpLanes() : state (std::make_unique<State>()) {}
CardArpLanes::~CardArpLanes() = default;

void CardArpLanes::prepare (double sampleRate)
{
    for (auto& a : state->laneArps)
        a.prepare (sampleRate);
    state->laneMidi.ensureSize (static_cast<std::size_t> (3 * Arpeggiator::maxEvents + 8192) * 16);
    state->legacyInput.ensureSize (static_cast<std::size_t> (4096 + 16 * 128) * 16);
    laneConfig = 0;
    laneMask = {};
    passMask = 0;
    forceOff = false;
    for (auto& row : state->heldKeys)
        row.fill (0);
    for (auto& row : state->passChannel)
        row.fill (0);
    for (auto& row : state->passSustained)
        row.fill (false);
    for (auto& row : state->soundingOut)
        row.fill (false);
    pedalDown = false;
}

void CardArpLanes::restartPatterns() noexcept
{
    for (auto& a : state->laneArps)
        a.restartPattern();
}

Arpeggiator& CardArpLanes::displayed (Host& host, int shownCard) noexcept
{
    // Unlinked lanes: the card the panel edits (A's is the arpeggiator's own).
    const int lane = laneConfig >= 0x200 ? std::clamp (shownCard, 0, 2) : 0;
    return lane == 0 ? host.mainArpeggiator() : state->laneArps[static_cast<std::size_t> (lane - 1)];
}

void CardArpLanes::trackOutput (const juce::MidiBuffer& out) noexcept
{
    // What the engine has been given and not yet ended, by channel: a change of lanes ends it.
    for (const auto metadata : out)
    {
        const auto m = metadata.getMessage();
        if (! m.isNoteOnOrOff())
            continue;
        const auto ch = static_cast<std::size_t> (std::clamp (m.getChannel(), 1, 16) - 1);
        state->soundingOut[ch][static_cast<std::size_t> (m.getNoteNumber())] = m.isNoteOn();
    }
}

void CardArpLanes::releaseSounding (juce::MidiBuffer& out) noexcept
{
    for (int ch = 0; ch < 16; ++ch)
        for (int n = 0; n < 128; ++n)
            if (state->soundingOut[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)])
            {
                out.addEvent (juce::MidiMessage::noteOff (ch + 1, n), 0);
                state->soundingOut[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] = false;
            }
    for (auto& row : state->passChannel)
        row.fill (0);
    for (auto& row : state->passSustained)
        row.fill (false);
}

const juce::MidiBuffer& CardArpLanes::process (Host& host, const juce::MidiBuffer& midi, int numSamples, const HostTiming& timing, const Controls& controls) noexcept
{
    auto& s = *state;
    // Which cards play the arpeggio (the others hold the keys), and whether they share A's
    // settings. LINK with every card, the arpeggiator off, or no card in it: the one
    // arpeggiator, exactly as before (no card left out: the keys as played).
    const std::uint8_t toggled = static_cast<std::uint8_t> (controls.cards & 7);
    const bool on = controls.enabled;
    const bool link = controls.link;
    const bool laned = on && toggled != 0 && ! (link && toggled == 7);
    const int config = laned ? (link ? 0x100 : 0x200) | toggled : 0;
    const bool changed = config != laneConfig;

    // The keys (always followed, so a change of lanes can play them again).
    auto followKeys = [this, &s] (const juce::MidiMessage& m) {
        if (m.isNoteOnOrOff())
        {
            const auto ch = static_cast<std::size_t> (std::clamp (m.getChannel(), 1, 16) - 1);
            s.heldKeys[ch][static_cast<std::size_t> (m.getNoteNumber())] = m.isNoteOn() ? static_cast<std::uint8_t> (std::max (1, static_cast<int> (m.getVelocity()))) : 0;
        }
        else if (m.isSustainPedalOn() || m.isSustainPedalOff())
            pedalDown = m.isSustainPedalOn();
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            for (auto& row : s.heldKeys)
                row.fill (0);
    };

    if (! laned && ! changed)
    {
        // The one arpeggiator (or the keys as played), as before.
        forceOff = on && toggled == 0;
        for (const auto metadata : midi)
            followKeys (metadata.getMessage());
        host.setCardRouting (false);
        const auto& out = host.runArpeggiator (midi, numSamples, timing);
        trackOutput (out);
        return out;
    }

    s.laneMidi.clear();
    if (changed)
    {
        // A new arrangement: what sounds ends, every arpeggiator starts over, and the keys
        // still down are played again into the new lanes (no note is ever left hanging).
        releaseSounding (s.laneMidi);
        host.mainArpeggiator().reset();
        for (auto& a : s.laneArps)
            a.reset();
        host.lanesRearranged();
        laneConfig = config;
    }

    if (! laned)
    {
        // Back to the one arpeggiator: it is given the keys still down first.
        forceOff = on && toggled == 0;
        host.setCardRouting (false);
        s.legacyInput.clear();
        for (int ch = 0; ch < 16; ++ch)
            for (int n = 0; n < 128; ++n)
                if (const int v = s.heldKeys[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]; v > 0)
                    s.legacyInput.addEvent (juce::MidiMessage::noteOn (ch + 1, n, static_cast<juce::uint8> (v)), 0);
        for (const auto metadata : midi)
        {
            followKeys (metadata.getMessage());
            s.legacyInput.addEvent (metadata.getMessage(), metadata.samplePosition);
        }
        const auto& out = host.runArpeggiator (s.legacyInput, numSamples, timing);
        for (const auto metadata : out)
            s.laneMidi.addEvent (metadata.getMessage(), metadata.samplePosition);
        trackOutput (s.laneMidi);
        return s.laneMidi;
    }

    // The lanes: LINK, one arpeggiator for the cards in it; unlinked, one per card in it
    // (A's settings are the arpeggiator's; B and C their own). The cards left out hold the keys.
    forceOff = false;
    host.setCardRouting (true);
    laneMask = {};
    if (link)
        laneMask[0] = toggled;
    else
        for (int c = 0; c < 3; ++c)
            if ((toggled & (1 << c)) != 0)
                laneMask[static_cast<std::size_t> (c)] = static_cast<std::uint8_t> (1 << c);
    passMask = static_cast<std::uint8_t> (~toggled & 7);

    auto laneArp = [&host, &s] (int lane) -> Arpeggiator& { return lane == 0 ? host.mainArpeggiator() : s.laneArps[static_cast<std::size_t> (lane - 1)]; };
    const auto base = host.arpSettings();
    const auto seed = controls.seed;
    for (int lane = 0; lane < 3; ++lane)
    {
        if (laneMask[static_cast<std::size_t> (lane)] == 0)
            continue;
        auto settings = base;
        settings.enabled = true;
        if (lane > 0)
        {
            const auto i = static_cast<std::size_t> (lane - 1);
            settings.pattern = static_cast<ArpPattern> (std::clamp (controls.pattern[i], 0, arp::patternCount - 1));
            settings.rate = static_cast<ArpRate> (std::clamp (controls.rate[i], 0, arp::rateCount - 1));
            settings.octaves = std::clamp (controls.octaves[i], arp::minOctaves, arp::maxOctaves);
            settings.octavesAtCycle = false;
        }
        auto& a = laneArp (lane);
        a.setSeed (lane == 0 ? seed : Prng::deriveSeed (seed, static_cast<std::uint64_t> (lane), 0x4c616e65));
        a.beginBlock (settings, timing, numSamples);
        if (changed)
        {
            for (int ch = 0; ch < 16; ++ch)
                for (int n = 0; n < 128; ++n)
                    if (const int v = s.heldKeys[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]; v > 0)
                        a.noteOn (0, n, v, ch + 1);
            if (pedalDown)
                a.sustainPedal (0, true);
        }
    }

    // The held keys on the cards left out (their own pedal: the engine's would hold the
    // arpeggio's notes too).
    const int passOut = 1 + passMask;
    auto passOn = [this, &s, passOut] (int channel, int note, int velocity, int offset) {
        if (passMask == 0)
            return;
        const auto ch = static_cast<std::size_t> (std::clamp (channel, 1, 16) - 1), n = static_cast<std::size_t> (note);
        if (s.passChannel[ch][n] != 0)   // (the same key again: its note ends first)
            s.laneMidi.addEvent (juce::MidiMessage::noteOff (s.passChannel[ch][n], note), offset);
        s.passChannel[ch][n] = static_cast<std::uint8_t> (passOut);
        s.passSustained[ch][n] = false;
        s.laneMidi.addEvent (juce::MidiMessage::noteOn (passOut, note, static_cast<juce::uint8> (velocity)), offset);
    };
    auto passOff = [this, &s] (int channel, int note, int offset) {
        const auto ch = static_cast<std::size_t> (std::clamp (channel, 1, 16) - 1), n = static_cast<std::size_t> (note);
        if (s.passChannel[ch][n] == 0)
            return;
        if (pedalDown)
        {
            s.passSustained[ch][n] = true;
            return;
        }
        s.laneMidi.addEvent (juce::MidiMessage::noteOff (s.passChannel[ch][n], note), offset);
        s.passChannel[ch][n] = 0;
    };
    auto releasePedal = [&s] (int offset) {
        for (int ch = 0; ch < 16; ++ch)
            for (int n = 0; n < 128; ++n)
                if (s.passSustained[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)])
                {
                    s.passSustained[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] = false;
                    s.laneMidi.addEvent (juce::MidiMessage::noteOff (s.passChannel[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)], n), offset);
                    s.passChannel[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)] = 0;
                }
    };
    if (changed && passMask != 0)
        for (int ch = 0; ch < 16; ++ch)
            for (int n = 0; n < 128; ++n)
                if (const int v = s.heldKeys[static_cast<std::size_t> (ch)][static_cast<std::size_t> (n)]; v > 0)
                    passOn (ch + 1, n, v, 0);

    for (const auto metadata : midi)
    {
        const auto m = metadata.getMessage();
        const int offset = std::clamp (metadata.samplePosition, 0, std::max (0, numSamples - 1));
        followKeys (m);
        bool consumed = true;
        for (int lane = 0; lane < 3; ++lane)
        {
            if (laneMask[static_cast<std::size_t> (lane)] == 0)
                continue;
            auto& a = laneArp (lane);
            if (m.isNoteOn())
                a.noteOn (offset, m.getNoteNumber(), m.getVelocity(), m.getChannel());
            else if (m.isNoteOff())
                a.noteOff (offset, m.getNoteNumber(), m.getChannel());
            else if (m.isSustainPedalOn() || m.isSustainPedalOff())
                a.sustainPedal (offset, m.isSustainPedalOn());
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                a.allNotesOff (offset);
        }
        if (m.isNoteOn())
            passOn (m.getChannel(), m.getNoteNumber(), m.getVelocity(), metadata.samplePosition);
        else if (m.isNoteOff())
            passOff (m.getChannel(), m.getNoteNumber(), metadata.samplePosition);
        else if (m.isSustainPedalOff())
            releasePedal (metadata.samplePosition);
        else if (! m.isSustainPedalOn())
            consumed = false;
        if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            for (auto& row : s.passChannel)
                row.fill (0);
            for (auto& row : s.passSustained)
                row.fill (false);
            consumed = false;
        }
        if (! consumed)
            s.laneMidi.addEvent (m, metadata.samplePosition);   // bends, wheels, pressure, CCs, All Notes Off
    }
    for (int lane = 0; lane < 3; ++lane)
    {
        const auto mask = laneMask[static_cast<std::size_t> (lane)];
        if (mask == 0)
            continue;
        auto& a = laneArp (lane);
        a.endBlock();
        const int channel = 1 + mask;
        for (int i = 0; i < a.numEvents(); ++i)
        {
            const auto& e = a.event (i);
            if (e.kind == Arpeggiator::Event::Kind::noteOn)
                s.laneMidi.addEvent (juce::MidiMessage::noteOn (channel, e.note, e.velocity), e.offset);
            else if (e.kind == Arpeggiator::Event::Kind::noteOff)
                s.laneMidi.addEvent (juce::MidiMessage::noteOff (channel, e.note), e.offset);
            // (its pedal events are its own: the engine's pedal would hold every lane)
        }
    }
    trackOutput (s.laneMidi);
    host.publishArpDisplay();
    return s.laneMidi;
}

} // namespace osp::plugin
