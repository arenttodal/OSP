// ARP style baseline (Custom ARP Patterns, CP0): every generated event of the 18 built-in
// styles, frozen before any custom-pattern work, so that work can prove the styles unchanged.
//
// Four scenes per style cover the scheduler's paths:
// - free running;
// - on a looping host grid with swing, a chord change and legato gate;
// - the modulated clock (ARP RATE off 1x) with a held single note over four octaves;
// - the sustain pedal keeping released keys in a five-note chord.
//
// The golden is tests/regression/golden/arp/arp-styles.txt (schemaVersion 1): one line per
// style and scene, every event as time:kind:note:velocity:channel. Kinds, notes, velocities,
// channels and order must match exactly; times may differ by one sample (a compiler's floating
// point contraction can move a boundary by one).
//
// Regenerate ONLY for an intentional change to a built-in style, and explain it in the commit:
//     OSP_UPDATE_GOLDEN=1 ./build/tests/osp_tests "[arp-style-baseline]"

#include "engine/Arpeggiator.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace osp;

namespace
{
    constexpr int schemaVersion = 1;

    struct Ev
    {
        std::int64_t time = 0;
        int kind = 0, note = 0, velocity = 0, channel = 1;
    };

    struct In
    {
        std::int64_t time = 0;
        enum class T { on, off, pedalDown, pedalUp } type = T::on;
        int note = 60, velocity = 100;
    };

    struct Scene
    {
        const char* name;
        double sampleRate;
        int block;
        bool host;
        double bpm;
        Arpeggiator::Settings settings;
        std::int64_t length;
        std::vector<In> inputs;
        /** Per block, before it starts: change settings or jump the host's position. */
        std::function<void (Arpeggiator::Settings&, double& ppq, std::int64_t now)> perBlock;
    };

    std::vector<Ev> render (const Scene& scene, ArpPattern pattern)
    {
        Arpeggiator arp;
        arp.setSeed (0x5eed);
        arp.prepare (scene.sampleRate);
        auto settings = scene.settings;
        settings.enabled = true;
        settings.pattern = pattern;
        auto inputs = scene.inputs;
        std::stable_sort (inputs.begin(), inputs.end(), [] (const In& a, const In& b) { return a.time < b.time; });
        std::vector<Ev> out;
        std::size_t next = 0;
        double ppq = 0.0;
        for (std::int64_t now = 0; now < scene.length;)
        {
            const int n = static_cast<int> (std::min<std::int64_t> (scene.block, scene.length - now));
            if (scene.perBlock)
                scene.perBlock (settings, ppq, now);
            HostTiming timing;
            timing.valid = scene.host;
            timing.playing = scene.host;
            timing.bpm = scene.bpm;
            timing.ppq = ppq;
            arp.beginBlock (settings, timing, n);
            while (next < inputs.size() && inputs[next].time < now + n)
            {
                const auto& in = inputs[next++];
                const int offset = static_cast<int> (std::max<std::int64_t> (0, in.time - now));
                switch (in.type)
                {
                    case In::T::on: arp.noteOn (offset, in.note, in.velocity, 1); break;
                    case In::T::off: arp.noteOff (offset, in.note, 1); break;
                    case In::T::pedalDown: arp.sustainPedal (offset, true); break;
                    case In::T::pedalUp: arp.sustainPedal (offset, false); break;
                }
            }
            arp.endBlock();
            for (int i = 0; i < arp.numEvents(); ++i)
            {
                const auto& e = arp.event (i);
                out.push_back ({ now + e.offset, static_cast<int> (e.kind), e.note, e.velocity, e.channel });
            }
            if (scene.host)
                ppq += n * scene.bpm / (60.0 * scene.sampleRate);
            now += n;
        }
        return out;
    }

    std::vector<In> chord (std::int64_t at, std::initializer_list<int> notes, int velocity = 100)
    {
        std::vector<In> r;
        int v = velocity;
        for (const int note : notes)
            r.push_back ({ at, In::T::on, note, v-- });   // distinct velocities: the order shows
        return r;
    }

    std::vector<Scene> scenes()
    {
        std::vector<Scene> s;
        {
            Scene free { "free-triad", 48000.0, 256, false, 120.0, {}, 48000 * 3, chord (1000, { 60, 64, 67 }), {} };
            free.settings.rate = ArpRate::sixteenth;
            free.settings.octaves = 2;
            free.settings.gate = 0.75;
            s.push_back (free);
        }
        {
            Scene host { "host-seventh-swing", 44100.0, 128, true, 97.0, {}, 44100 * 4, chord (300, { 60, 64, 67, 71 }, 110), {} };
            host.settings.rate = ArpRate::eighthTriplet;
            host.settings.octaves = 3;
            host.settings.gate = 1.2;
            host.settings.swing = 0.4;
            // The chord changes: E released, D added (the pattern follows the new pool).
            host.inputs.push_back ({ 44100 * 2, In::T::off, 64, 0 });
            host.inputs.push_back ({ 44100 * 2 + 50, In::T::on, 62, 90 });
            // The host loops back two beats once.
            host.perBlock = [] (Arpeggiator::Settings&, double& ppq, std::int64_t now) {
                if (now == 128 * 1000)
                    ppq -= 2.0;
            };
            s.push_back (host);
        }
        {
            Scene mod { "modclock-single-4oct", 48000.0, 64, false, 133.0, {}, 48000 * 2, chord (0, { 57 }, 96), {} };
            mod.settings.rate = ArpRate::thirtySecond;
            mod.settings.octaves = 4;
            mod.settings.gate = 0.5;
            mod.settings.velocityScale = 0.8;
            mod.perBlock = [] (Arpeggiator::Settings& st, double&, std::int64_t now) {
                st.rateMultiplier = now < 48000 ? 1.5 : 0.75;
            };
            s.push_back (mod);
        }
        {
            Scene pedal { "pedal-five", 48000.0, 512, false, 110.0, {}, 48000 * 3, chord (0, { 48, 55, 60, 64, 67 }), {} };
            pedal.settings.rate = ArpRate::eighth;
            pedal.settings.octaves = 1;
            pedal.settings.gate = 0.3;
            pedal.inputs.push_back ({ 20000, In::T::pedalDown, 0, 0 });
            pedal.inputs.push_back ({ 30000, In::T::off, 55, 0 });
            pedal.inputs.push_back ({ 30000, In::T::off, 64, 0 });
            pedal.inputs.push_back ({ 90000, In::T::pedalUp, 0, 0 });
            s.push_back (pedal);
        }
        return s;
    }

    std::string encode (const std::vector<Ev>& events)
    {
        std::ostringstream o;
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            const auto& e = events[i];
            o << (i ? " " : "") << e.time << ':' << e.kind << ':' << e.note << ':' << e.velocity << ':' << e.channel;
        }
        return o.str();
    }

    std::vector<Ev> decode (const std::string& text)
    {
        std::vector<Ev> r;
        std::istringstream in (text);
        std::string token;
        while (in >> token)
        {
            Ev e;
            char c;
            std::istringstream t (token);
            t >> e.time >> c >> e.kind >> c >> e.note >> c >> e.velocity >> c >> e.channel;
            r.push_back (e);
        }
        return r;
    }

    std::filesystem::path goldenFile()
    {
        return std::filesystem::path (OSP_SOURCE_DIR) / "tests" / "regression" / "golden" / "arp" / "arp-styles.txt";
    }
}

TEST_CASE ("ARP style baseline: the 18 built-in styles produce exactly their frozen events", "[regression][arp][arp-style-baseline]")
{
    std::map<std::string, std::string> rendered;
    std::vector<std::string> order;
    for (const auto& scene : scenes())
        for (int p = 0; p < arp::patternCount; ++p)
        {
            const auto key = std::string (arp::patternName (static_cast<ArpPattern> (p))) + "|" + scene.name;
            const auto events = render (scene, static_cast<ArpPattern> (p));
            REQUIRE (! events.empty());
            rendered[key] = encode (events);
            order.push_back (key);
        }

    const bool update = std::getenv ("OSP_UPDATE_GOLDEN") != nullptr && std::string (std::getenv ("OSP_UPDATE_GOLDEN")) == "1";
    if (update)
    {
        std::filesystem::create_directories (goldenFile().parent_path());
        std::ofstream out (goldenFile());
        out << "# ARP style baseline: tests/regression/ArpStyleBaselineTests.cpp\n";
        out << "schemaVersion " << schemaVersion << "\n";
        for (const auto& key : order)
            out << key << "|" << rendered[key] << "\n";
        SUCCEED ("golden written");
        return;
    }

    std::ifstream in (goldenFile());
    INFO ("missing " << goldenFile().string() << " (OSP_UPDATE_GOLDEN=1 writes it)");
    REQUIRE (in.good());
    std::map<std::string, std::string> golden;
    std::string line;
    int version = 0;
    while (std::getline (in, line))
    {
        if (line.empty() || line[0] == '#')
            continue;
        if (line.rfind ("schemaVersion ", 0) == 0)
        {
            version = std::stoi (line.substr (14));
            continue;
        }
        const auto a = line.find ('|');
        const auto b = line.find ('|', a + 1);
        REQUIRE (b != std::string::npos);
        golden[line.substr (0, b)] = line.substr (b + 1);
    }
    REQUIRE (version == schemaVersion);
    REQUIRE (golden.size() == rendered.size());

    for (const auto& key : order)
    {
        INFO (key);
        REQUIRE (golden.count (key) == 1);
        const auto want = decode (golden[key]);
        const auto got = decode (rendered[key]);
        REQUIRE (got.size() == want.size());
        for (std::size_t i = 0; i < got.size(); ++i)
        {
            INFO ("event " << i);
            CHECK (got[i].kind == want[i].kind);
            CHECK (got[i].note == want[i].note);
            CHECK (got[i].velocity == want[i].velocity);
            CHECK (got[i].channel == want[i].channel);
            CHECK (std::llabs (got[i].time - want[i].time) <= 1);
        }
    }
}
