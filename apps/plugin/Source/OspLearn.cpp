// ANDOR Learn in ANDOR OSP: the shared controller (shared/andor-learn, as ANDOR ANALOG's) on
// OSP's own editor. Here: the lessons and the Info texts, the semantic targets (the real
// controls of the native editor, found again each time they are needed), the source-readiness
// states, the user-action hooks and the tutorial examples. Nothing here writes a parameter;
// every action that completes a step comes from a control's own handler or from a parameter
// gesture the editor's controls began (ParameterGestureBridge).

#include "PluginEditor.h"

#include "Binding.h"
#include "TutorialSounds.h"

#include "engine/KeyDna.h"

namespace osp::plugin
{

using andor::learn::Catalog;
using andor::learn::Completion;
using andor::learn::Condition;
using andor::learn::HelpTopic;
using andor::learn::Lesson;
using andor::learn::Step;
using andor::learn::Target;
using andor::learn::TargetRegistry;

namespace
{
    // The edited layer's parameters in lessons: "layer{L}.start" is the layer being edited now.
    const juce::String editedLayerToken = "layer{L}";
    const juce::String reimaginedToken = "layer{L}.reimagined";

    juce::String param (const juce::String& id) { return "param:" + id; }

    /** The first visible component under `root` (itself included) tagged with `id`. */
    juce::Component* findTagged (juce::Component* root, const juce::String& id)
    {
        if (root == nullptr)
            return nullptr;
        if (binding::tagsOf (*root).contains (id) || root->getProperties()["paramId"].toString() == id)
            return root;
        for (auto* child : root->getChildren())
            if (child->isVisible())
                if (auto* found = findTagged (child, id))
                    return found;
        return nullptr;
    }

    Step step (juce::String id, juce::String target, juce::String title, juce::String body, Completion completion = Completion::manual())
    {
        Step s;
        s.id = std::move (id);
        s.targetId = std::move (target);
        s.title = std::move (title);
        s.body = std::move (body);
        s.completion = std::move (completion);
        return s;
    }

    Completion edited (const juce::String& parameterId)
    {
        return Completion::action ({ param (parameterId) }, Condition::changed (parameterId));
    }

    Completion setTo (const juce::String& parameterId, float value, const juce::String& already)
    {
        return Completion::action ({ param (parameterId) }, Condition::equals (parameterId, value)).withAlreadyDone (Condition::equals (parameterId, value), already);
    }

    using andor::learn::Requirement;

    /** The edited layer has a recording, loaded and playable. */
    Requirement soundReady()
    {
        Requirement r;
        r.id = "osp.sound";
        r.holds = Condition::state ("osp.source.ready");
        r.title = "A recording is needed";
        r.body = juce::String::fromUTF8 ("This step plays the recording on the layer you are editing. Load one (drag an audio file onto OSP, or Browse\xe2\x80\xa6), or load the lesson's example sound.");
        r.targetId = "osp.source.dropZone";
        return r;
    }

    Requirement granular()
    {
        Requirement r;
        r.id = "osp.granular";
        r.holds = Condition::state ("osp.source.granular");
        r.title = "Granular is needed";
        r.body = "This control belongs to Granular playback. Choose Granular on the layer's playback key (the symbol key at the top right of the card).";
        r.targetId = "osp.source.mode";
        r.offerExample = false;
        return r;
    }

    Requirement twoLayers()
    {
        Requirement r;
        r.id = "osp.layers";
        r.holds = Condition::state ("osp.layers.two");
        r.title = "A second recording is needed";
        r.body = juce::String::fromUTF8 ("Open the \xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2 menu and choose Add layer\xe2\x80\xa6, or drag a second file beside the card: ADD LAYER appears while you drag.");
        r.targetId = "osp.header.menu";
        return r;
    }

    Requirement sampleSet()
    {
        Requirement r;
        r.id = "osp.set";
        r.holds = Condition::state ("osp.source.set");
        r.title = "Several recordings are needed";
        r.body = "This step needs several recordings loaded together as one sound on this layer. Drop a folder of recordings on a layer, or load the pluck set.";
        return r;
    }

    /** The lesson's set-up: its sound first (the current one, or the example). */
    Step prepare (std::vector<Requirement> needs, juce::String body)
    {
        Step s;
        s.id = "setup";
        s.title = "Set up the lesson";
        s.body = std::move (body);
        s.preparation = true;
        s.requirements = std::move (needs);
        return s;
    }

    /** Every step after the set-up checks what it needs on the real state. */
    void require (Lesson& l, std::vector<Requirement> needs, int fromStep = 1)
    {
        for (int i = fromStep; i < static_cast<int> (l.steps.size()); ++i)
            for (const auto& r : needs)
                l.steps[static_cast<std::size_t> (i)].requirements.push_back (r);
    }

    /** A lesson's set-up step first (what it needs), then every step checks it. */
    void setUp (Lesson& l, std::vector<Requirement> needs, juce::String body)
    {
        l.steps.insert (l.steps.begin(), prepare (needs, std::move (body)));
        require (l, std::move (needs));
    }

    void requireOn (Lesson& l, const juce::StringArray& stepIds, const Requirement& r)
    {
        for (auto& st : l.steps)
            if (stepIds.contains (st.id))
                st.requirements.push_back (r);
    }

    juce::StringArray routeDepthActions()
    {
        juce::StringArray out;
        for (int r = 0; r < mod::maxRoutes; ++r)
            out.add (param (InstrumentHost::modRouteId (r, "depth")));
        return out;
    }
} // namespace

//==============================================================================
// The curriculum: the recording-to-instrument workflow (docs/learn/osp-learn.md has the
// editorial checks behind each sentence).

static Catalog ospCatalog()
{
    Catalog c;
    c.productId = "osp";
    c.quickStartLessonId = "osp.quickstart";
    const auto ready = Condition::state ("osp.source.ready");

    {
        Lesson l;
        l.id = "osp.quickstart";
        l.title = "Quick Start";
        l.description = "Turn a recording into a playable instrument.";
        l.minutes = 3;
        l.exampleId = "bowed";
        // The first step is the set-up: a recording loaded and playable (checked on the real
        // state: a dropped file counts once it has loaded and been analysed), or the example.
        auto load = prepare ({ soundReady() },
                             juce::String::fromUTF8 ("Drag an audio file onto the empty sound area, or click Browse\xe2\x80\xa6 to choose one. One tonal recording is enough. Or load the example sound."));
        load.title = "Load a recording";
        load.targetId = "osp.source.dropZone";
        l.steps.push_back (load);
        l.steps.push_back (step ("play", "osp.keyboard", "Play it from the keyboard",
                                 "Play a few notes on your MIDI keyboard or on the keyboard at the bottom of OSP. The recording is pitched to each note you play.",
                                 Completion::action ({ "note.played" }, ready)));
        l.steps.push_back (step ("start", "osp.source.start", "Change the starting point",
                                 "START chooses where in the recording each note begins. Move it and play again to hear a later part of the sound.",
                                 edited (editedLayerToken + ".start")));
        l.steps.push_back (step ("level", "osp.source.level", "Adjust the level",
                                 "LEVEL sets how loud this layer is. Turn it down and back up while you play.", edited (editedLayerToken + ".level")));
        l.steps.push_back (step ("reimagined", "osp.source.reimagined", "Try REIMAGINED",
                                 "REIMAGINED reinterprets the recording in the mode chosen for this layer. Hold a note and turn it up slowly.",
                                 edited (reimaginedToken)));
        auto back = step ("original", "osp.source.reimagined", "Hear the original again",
                          "Turn REIMAGINED back to 0 %. At 0 % the layer plays without it; any amount in between is yours to choose.",
                          Completion::action ({ param (reimaginedToken) }, Condition::equals (reimaginedToken, 0.0f))
                              .withAlreadyDone (Condition::equals (reimaginedToken, 0.0f), "Already at 0 %"));
        l.steps.push_back (back);
        l.completionText = "You have made a playable instrument from one recording. The next lessons change how it plays, add variation and combine sounds.";
        require (l, { soundReady() });
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.playback";
        l.title = "One Shot & Granular";
        l.description = "Two ways of playing the same recording.";
        l.minutes = 4;
        l.exampleId = "bowed";
        const auto mode = editedLayerToken + ".sourceMode";
        l.steps.push_back (step ("selector", "osp.source.mode", "The playback key",
                                 "The symbol key at the top right of the card, beside EQ, chooses how OSP plays this layer: One Shot or Granular. Click it to see both."));
        l.steps.push_back (step ("oneshot", "osp.source.mode", "Play in One Shot",
                                 "Choose One Shot. Each note plays the recording from START, at the pitch of the note.", setTo (mode, 0.0f, "Already One Shot")));
        l.steps.push_back (step ("granular", "osp.source.mode", "Switch to Granular",
                                 "Choose Granular. OSP now builds each note from many short, overlapping grains of the recording instead of playing it from start to end.",
                                 setTo (mode, 1.0f, "Already Granular")));
        l.steps.push_back (step ("pos", "osp.granular.position", "Change the position",
                                 "POS chooses where in the recording the grains are taken. Hold a note and move it.", edited (editedLayerToken + ".granular.position")));
        l.steps.push_back (step ("size", "osp.granular.size", "Change the grain size",
                                 "SIZE sets how long each grain lasts, from 20 to 400 ms. Short grains sound more broken up; long grains keep more of the recording's own movement.",
                                 edited (editedLayerToken + ".granular.size")));
        l.steps.push_back (step ("dens", "osp.granular.density", "Change the density",
                                 "DENS sets how many grains start each second. Raise it and listen to the grains overlap into a denser sound.",
                                 edited (editedLayerToken + ".granular.density")));
        l.steps.push_back (step ("spread", "osp.granular.spread", "Change the spread",
                                 "SPREAD sets how far around POS the grains are taken. Raise it to draw on more of the recording.",
                                 edited (editedLayerToken + ".granular.spread")));
        l.steps.push_back (step ("compare", "osp.source.mode", "Compare the two",
                                 "Switch between One Shot and Granular and play the same notes. The recording stays the same; only the way it is played changes, from the next note on.",
                                 edited (mode)));
        l.completionText = "One Shot plays through the recording; Granular builds the sound from overlapping grains. Each layer has its own playback mode.";
        setUp (l, { soundReady() }, "This lesson plays a recording on the layer you are editing: use your current sound, or the bowed example.");
        requireOn (l, { "pos", "size", "dens", "spread" }, granular());
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.reimagined";
        l.title = "REIMAGINED";
        l.description = "Transform a recording while keeping its character.";
        l.minutes = 5;
        l.exampleId = "bowed";
        const auto modeId = editedLayerToken + ".reimagined.mode";
        l.steps.push_back (step ("find", "osp.source.reimagined", "Find REIMAGINED",
                                 "Each layer has its own REIMAGINED knob. At 0 % the layer plays without it; turning it up brings in the mode chosen for the layer."));
        auto open = step ("open", "osp.source.reimagined.label", "Open the modes", "Click the word REIMAGINED above the knob to open its modes.",
                          Completion::action ({ "reimagined.opened" }, Condition::state ("osp.popup.reimagined.open"))
                              .withAlreadyDone (Condition::state ("osp.popup.reimagined.open"), "Already open"));
        open.revealOnEnter = false;
        l.steps.push_back (open);
        const char* names[] = { "KALEIDOSCOPE", "TAPE FRAME", "TOYBOX", "MOSAIC", "MIRAGE" };
        const char* bodies[] = {
            "Choose KALEIDOSCOPE, the original REIMAGINED: the recording is refracted into related variations, with doubling, saturation and grains arriving as the amount rises.",
            "Choose TAPE FRAME. The recording is laid on a short tape played at tape speed, so lower notes play slower and darker. AGE adds wear; STABILITY adds wow and flutter.",
            "Choose TOYBOX. The recording plays from a small, low-resolution digital memory; PLAY lets its read head run forward, turn back and forth, or jump.",
            "Choose MOSAIC. The note is rebuilt from harmonic frames analysed from the recording, at the pitch you play; MOTION travels through the frames.",
            "Choose MIRAGE. The recording plays through an early-digital sampler with a variable clock, into a resonant low-pass FILTER.",
        };
        for (int m = 0; m < 5; ++m)
        {
            auto s = step ("mode" + juce::String (m), "osp.reimagined.mode", juce::String ("Try ") + names[m], bodies[m],
                           setTo (modeId, static_cast<float> (m), "Already chosen"));
            s.unavailableBody = "The REIMAGINED modes are closed. Show me opens them again.";
            l.steps.push_back (s);
        }
        l.steps.push_back (step ("amount", "osp.source.reimagined", "Set the amount",
                                 "Keep the mode you like and set how much of it you hear with REIMAGINED. Each layer has its own mode and amount.", edited (reimaginedToken)));
        l.completionText = "REIMAGINED offers five ways to transform the same recording. Changing the mode never reloads or alters the recording itself.";
        setUp (l, { soundReady() }, "This lesson transforms a tonal recording: use your current sound, or the bowed example.");
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.layers";
        l.title = "Layering sounds";
        l.description = "Combine two or three recordings in one instrument.";
        l.minutes = 4;
        l.exampleId = "layered";
        const auto two = Condition::state ("osp.layers.two");
        // (The second recording: the step waits, blocked, until two layers can play.)
        auto add = step ("add", "osp.header.menu", "Add another recording",
                         "Both recordings are loaded and play as layers A and B. Continue to hear them together.");
        add.requirements.push_back (twoLayers());
        l.steps.push_back (add);
        l.steps.push_back (step ("play", "osp.keyboard", "Play both", "Play a note. The layers sound together, each from its own recording.",
                                 Completion::action ({ "note.played" }, two)));
        l.steps.push_back (step ("balance", "osp.source.B.level", "Balance them", "Use layer B's LEVEL to set how much of it you hear against layer A.",
                                 edited ("layerB.level")));
        l.steps.push_back (step ("solo", "osp.source.B.solo", "Solo a layer", "Click S on layer B to hear it alone.", setTo ("layerB.solo", 1.0f, "Already soloed")));
        l.steps.push_back (step ("unsolo", "osp.source.B.solo", "Back together", "Click S again to turn Solo off and hear both layers.",
                                 setTo ("layerB.solo", 0.0f, "Solo is off")));
        l.steps.push_back (step ("tune", "osp.source.B.tune", "Tune a layer",
                                 "TUNE shifts layer B's pitch against the notes you play, in semitones. Try a small change and hear how the two recordings sit together.",
                                 edited ("layerB.tune")));
        const auto differ = Condition::any ({ Condition::all ({ Condition::equals ("layerA.sourceMode", 0.0f), Condition::equals ("layerB.sourceMode", 1.0f) }),
                                              Condition::all ({ Condition::equals ("layerA.sourceMode", 1.0f), Condition::equals ("layerB.sourceMode", 0.0f) }) });
        l.steps.push_back (step ("modes", "osp.source.B.mode", "Different playback per layer",
                                 "Switch layer B to Granular while layer A stays in One Shot (or the other way round). Each layer has its own playback mode.",
                                 Completion::action ({ param ("layerB.sourceMode") }, differ).withAlreadyDone (differ, "Already different")));
        l.steps.push_back (step ("third", "osp.header.menu", "A third layer",
                                 "OSP holds up to three layers. Add a third recording the same way whenever you want a richer instrument."));
        l.completionText = "Each layer has its own recording and its own playback controls. Together they play as one instrument.";
        setUp (l, { soundReady() }, "This lesson starts from one recording and adds a second. The example loads two contrasting sounds at once.");
        requireOn (l, { "play", "balance", "solo", "unsolo", "tune", "modes" }, twoLayers());
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.multisample";
        l.title = "Multi-sample instruments";
        l.description = "Build one instrument from several recordings.";
        l.minutes = 4;
        l.exampleId = "pluckSet";
        l.steps.push_back (step ("set", "osp.source.card", "One sound from many files",
                                 "Files dropped together, or a folder, become one sound on one layer. OSP sorts them by their detected pitch."));
        l.steps.push_back (step ("play", "osp.keyboard", "Play across the keyboard",
                                 "Play low and high notes. Each key plays the recording nearest in pitch, transposed the rest of the way; repeated notes take turns between takes of the same pitch.",
                                 Completion::action ({ "note.played" }, Condition::state ("osp.source.set"))));
        auto samples = step ("samples", "osp.source.menu", "Open Samples",
                             juce::String::fromUTF8 ("Open the layer's \xe2\x8b\xaf menu and choose Samples to see how OSP has placed each file."),
                             Completion::action ({ "samples.opened" }, Condition::state ("osp.samples.open")).withAlreadyDone (Condition::state ("osp.samples.open"), "Already open"));
        samples.revealOnEnter = false;
        l.steps.push_back (samples);
        auto root = step ("root", "osp.samples.root", "The root note",
                          "The root is the pitch OSP believes a file was recorded at, and maps it by. If one was detected wrongly, choose the right note here.");
        root.unavailableBody = "The Samples inspector is closed. Show me opens it again.";
        l.steps.push_back (root);
        auto role = step ("role", "osp.samples.role", "Takes and roles",
                          "Files of the same pitch are Round robin takes, played in turn. Pitch is the only file of its note; Articulation is kept out of normal notes.");
        role.unavailableBody = root.unavailableBody;
        l.steps.push_back (role);
        auto vel = step ("velocity", "osp.samples.layer", "Velocity layers",
                         "Layer 1 to 4 is the velocity layer: files of one pitch played at different strengths. OSP makes layers from dynamics words in the names, such as pp, mf or ff.");
        vel.unavailableBody = root.unavailableBody;
        l.steps.push_back (vel);
        l.completionText = "A multi-sample sound gives OSP a recording near every note. Samples shows how each file is used, and lets you correct it.";
        setUp (l, { sampleSet() }, "This lesson needs several recordings loaded as one sound: a folder dropped on a layer, or the pluck set example.");
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.life";
        l.title = "LIFE: natural variation";
        l.description = "Make repeated notes less identical.";
        l.minutes = 4;
        l.exampleId = "pluck";
        l.steps.push_back (step ("zero", "osp.macro.life", "Repeat a note", "Turn LIFE to 0 % and play the same note several times. Each repeat is performed the same way.",
                                 setTo ("life", 0.0f, "LIFE is at 0 %")));
        l.steps.push_back (step ("raise", "osp.macro.life", "Turn LIFE up",
                                 "Turn LIFE up and repeat the note. Each note is now performed a little differently: its pitch, tone and attack vary within limits.",
                                 edited ("life")));
        auto open = step ("open", "osp.macro.life.label", "Open LIFE", "Click LIFE above the knob to open its settings.",
                          Completion::action ({ "macro.0.opened" }, Condition::state ("osp.popup.life.open")).withAlreadyDone (Condition::state ("osp.popup.life.open"), "Already open"));
        open.revealOnEnter = false;
        l.steps.push_back (open);
        const juce::String closed = "LIFE's settings are closed. Show me opens them again.";
        auto pitch = step ("pitch", "osp.life.pitch", "Pitch", "PITCH sets how far each note may stray from its pitch, up to 15 cents. Change it and repeat a note.",
                           edited ("life.pitch"));
        pitch.unavailableBody = closed;
        l.steps.push_back (pitch);
        auto tone = step ("tone", "osp.life.tone", "Tone", "TONE sets how much the brightness of each note may differ. Change it and repeat a note.", edited ("life.tone"));
        tone.unavailableBody = closed;
        l.steps.push_back (tone);
        auto attack = step ("attack", "osp.life.attack", "Attack", "ATTACK sets how much the start of each note may differ. Change it and repeat a note.",
                            edited ("life.attack"));
        attack.unavailableBody = closed;
        l.steps.push_back (attack);
        auto takes = step ("takes", "osp.life.takes", "Takes",
                           juce::String::fromUTF8 ("With TAKES at \xe2\x88\x9e every note is new. Choose a number, such as 4: each note keeps that many fixed performances and returns to them. ORDER cycles them or picks at random."),
                           edited ("life.takes"));
        takes.unavailableBody = closed;
        l.steps.push_back (takes);
        l.completionText = "LIFE varies how each note is performed. With one recording it does not create new recordings: it plays the same one a little differently each time.";
        setUp (l, { soundReady() }, "LIFE is easiest to hear on short, repeated notes: use your current sound, or the plucked example.");
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.keydna";
        l.title = "KEY DNA";
        l.description = "Give each key its own granular character.";
        l.minutes = 4;
        l.exampleId = "bowed";
        l.steps.push_back (step ("granular", "osp.source.mode", "Switch to Granular",
                                 "KEY DNA belongs to Granular playback. Choose Granular on this layer's playback key.",
                                 setTo (editedLayerToken + ".sourceMode", 1.0f, "Already Granular")));
        auto open = step ("open", "osp.source.keyDna", "Open KEY DNA",
                          "Click KEY DNA at the end of the granular controls (they show while the pointer is over the card's display).",
                          Completion::action ({ "keydna.opened" }, Condition::state ("osp.keydna.open")).withAlreadyDone (Condition::state ("osp.keydna.open"), "Already open"));
        l.steps.push_back (open);
        const juce::String closed = "KEY DNA is closed. Show me opens it again.";
        auto what = step ("what", "osp.keydna.graph", "What it does",
                          "KEY DNA gives every key its own small offsets of the granular settings, POS, SIZE, DENS and others, fixed when the note starts. One Shot playback is not affected.");
        what.unavailableBody = closed;
        l.steps.push_back (what);
        auto power = step ("on", "osp.keydna.power", "Switch it on", "Click OFF to switch KEY DNA on for this layer.",
                           Completion::action ({ "keydna.power" }, Condition::state ("osp.keydna.enabled")).withAlreadyDone (Condition::state ("osp.keydna.enabled"), "Already on"));
        power.unavailableBody = closed;
        l.steps.push_back (power);
        auto pos = step ("position", "osp.keydna.depth", "Vary the position",
                         "With POS chosen, drag DEPTH and play keys across the keyboard: each key now takes its grains from a slightly different place.",
                         Completion::action ({ "keydna.depth.position", "keydna.curve.position", "keydna.keys.position", "keydna.variation.position" }, Condition::always()));
        pos.unavailableBody = closed;
        l.steps.push_back (pos);
        auto other = step ("other", "osp.keydna.tabs", "Try another setting",
                           "Choose SIZE or DENS above the graph and change its DEPTH or VARIATION. Change one at a time so you can hear what it does.",
                           Completion::action ({ "keydna.depth.size", "keydna.variation.size", "keydna.curve.size", "keydna.depth.density", "keydna.variation.density", "keydna.curve.density" },
                                               Condition::always()));
        other.unavailableBody = closed;
        l.steps.push_back (other);
        auto amount = step ("amount", "osp.keydna.amount", "Set the amount", "AMOUNT scales all of this layer's KEY DNA at once, from none to the full offsets you set.",
                            edited (editedLayerToken + ".keyDna.amount"));
        amount.unavailableBody = closed;
        l.steps.push_back (amount);
        l.completionText = "KEY DNA varies the granular sound from key to key. The recording and its One Shot playback are left as they are.";
        setUp (l, { soundReady() }, "KEY DNA shapes Granular playback of a recording: use your current sound, or the bowed example.");
        requireOn (l, { "open", "what", "on", "position", "other", "amount" }, granular());
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.arp";
        l.title = "Arpeggiator";
        l.description = "Play held notes as a repeating pattern.";
        l.minutes = 3;
        l.exampleId = "pluck";
        auto open = step ("open", "osp.arp.control", "Open ARP", "Click ARP beside the keyboard to open the arpeggiator.",
                          Completion::action ({ andor::learn::actions::arpPanelOpened }, Condition::state ("osp.arp.panel.open"))
                              .withAlreadyDone (Condition::state ("osp.arp.panel.open"), "Already open"));
        open.revealOnEnter = false;
        l.steps.push_back (open);
        l.steps.push_back (step ("on", "osp.arp.enable", "Switch it on", "Click ARP's light to switch it on, then hold a few notes to hear them played in turn.",
                                 Completion::action ({ andor::learn::actions::arpEnabledChanged, param ("arp.enabled") }, Condition::equals ("arp.enabled", 1.0f))
                                     .withAlreadyDone (Condition::equals ("arp.enabled", 1.0f), "Already on")));
        l.steps.push_back (step ("pattern", "osp.arp.pattern", "Change the pattern", "PATTERN sets the order of the notes. Try Up, Down and Up/Down.",
                                 Completion::action ({ andor::learn::actions::arpPatternChanged, param ("arp.pattern") }, Condition::changed ("arp.pattern"))));
        l.steps.push_back (step ("rate", "osp.arp.rate", "Change the speed", "RATE sets how quickly the notes follow each other. Compare 1/8 and 1/16.",
                                 Completion::action ({ andor::learn::actions::arpRateChanged, param ("arp.rate") }, Condition::changed ("arp.rate"))));
        l.steps.push_back (step ("octaves", "osp.arp.octaves", "Change the range", "OCTAVES sets how many octaves the pattern climbs through, from 1 to 4.",
                                 Completion::action ({ andor::learn::actions::arpOctavesChanged, param ("arp.octaves") }, Condition::changed ("arp.octaves"))));
        l.steps.push_back (step ("gate", "osp.arp.gate", "Change the note length", "GATE sets how long each note is held before it is released, as a share of the step.",
                                 edited ("arp.gate")));
        l.completionText = "The arpeggiator turns the notes you hold into a repeating sequence. The recording itself is not changed.";
        setUp (l, { soundReady() }, "The arpeggiator plays the notes you hold: use your current sound, or the plucked example.");
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.sub";
        l.title = "SUB: a bass under your keys";
        l.description = "Add a sub-bass that follows the keys you hold.";
        l.minutes = 2;
        l.exampleId = "bowed";
        auto open = step ("open", "osp.sub.row", "Open SUB", "SUB shares the arpeggiator's key beside the keyboard. Click SUB, the key's lower row, to open its settings.",
                          Completion::action ({ "sub.opened" }, Condition::state ("osp.popup.sub.open")).withAlreadyDone (Condition::state ("osp.popup.sub.open"), "Already open"));
        open.revealOnEnter = false;
        l.steps.push_back (open);
        l.steps.push_back (step ("on", "osp.sub.enable", "Switch it on", "Click SUB's light to switch it on. It plays one or two octaves below each key you hold.",
                                 setTo ("sub.enabled", 1.0f, "Already on")));
        l.steps.push_back (step ("play", "osp.keyboard", "Hold a chord", "Hold a few low notes. SUB follows the keys you hold, even while the arpeggiator plays them in turn, and it is never sent to ECHO or SPACE.",
                                 Completion::action ({ "note.played" }, Condition::equals ("sub.enabled", 1.0f))));
        l.steps.push_back (step ("level", "osp.sub.level", "Set its level", "LEVEL sets how loud SUB plays under the sound.", edited ("sub.level")));
        l.steps.push_back (step ("range", "osp.sub.range", "Choose its keys",
                                 "SUB plays only the keys inside the lit range. Drag either end on the keyboard, or the range itself to move it. With TAPER, higher keys play quieter.",
                                 Completion::action ({ param ("sub.lowKey"), param ("sub.highKey") }, Condition::state ("osp.popup.sub.open"))));
        l.completionText = "SUB adds a sub-bass under the keys you hold. It follows FILTER's low-pass and MOVEMENT's PULSE and SHAPER, and stays out of ECHO and SPACE.";
        // (SUB plays without a recording: the set-up only offers one to hear it under.)
        setUp (l, {}, "SUB plays under any sound, or on its own: use your current sound, or the bowed example.");
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.modulation";
        l.title = "Modulation";
        l.description = "Make a control move by itself.";
        l.minutes = 4;
        l.exampleId = "bowed";
        const auto onLfo = Condition::state ("osp.mod.tab.lfo1");
        auto tab = step ("tab", "osp.mod.lfo1", "Select LFO 1",
                         "Click LFO 1 in the modulation panel. An LFO is a slow, repeating movement that can move other controls.",
                         Completion::action ({ "mod.tab.lfo1" }, onLfo).withAlreadyDone (onLfo, "Already selected"));
        tab.revealOnEnter = false;
        l.steps.push_back (tab);
        l.steps.push_back (step ("shape", "osp.mod.lfo1.shape", "Choose the movement", "SHAPE sets the LFO's pattern of movement. Start with SINE, a smooth rise and fall.",
                                 setTo ("mod.lfo1.shape", 0.0f, "Already SINE")));
        l.steps.push_back (step ("rate", "osp.mod.lfo1.rate", "Set the rate", "RATE sets how quickly the LFO moves. Start slowly, so each change is easy to hear.",
                                 Completion::action ({ param ("mod.lfo1.rate"), param ("mod.lfo1.division") },
                                                     Condition::any ({ Condition::changed ("mod.lfo1.rate"), Condition::changed ("mod.lfo1.division") }))));
        auto assign = step ("assign", "osp.mod.lfo1", "Connect it",
                            "Drag LFO 1's tab onto a knob, such as FILTER. A cable follows the pointer; letting go connects LFO 1 to that knob at 50 % depth.",
                            Completion::action ({ "mod.route.created" }, Condition::state ("osp.mod.lfo1.routed"))
                                .withAlreadyDone (Condition::state ("osp.mod.lfo1.routed"), "Already connected"));
        assign.revealOnEnter = false;
        l.steps.push_back (assign);
        l.steps.push_back (step ("depth", "osp.mod.halo", "Set how far it moves",
                                 "The coloured ring around the knob shows how far LFO 1 moves it. Drag the ring up or down to change the depth; the knob itself stays where you set it.",
                                 Completion::action (routeDepthActions(), Condition::state ("osp.mod.lfo1.routed"))));
        l.steps.push_back (step ("listen", "osp.mod.halo", "Watch and listen",
                                 "Play a note. The ring moves as LFO 1 changes the setting, while the knob keeps its own position."));
        l.completionText = "Modulation moves a setting while you play. Other sources, such as ENV, VEL and the mod wheel, connect to controls the same way.";
        setUp (l, { soundReady() }, "Modulation moves a control while you play: use your current sound, or the bowed example.");
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.sound";
        l.title = "Shape the sound";
        l.description = "The filter, saturation, movement, echo and space.";
        l.minutes = 4;
        l.exampleId = "bowed";
        l.steps.push_back (step ("filter", "osp.macro.filter", "FILTER",
                                 "FILTER sets the cutoff of the voices' filter, between its MIN and MAX. Lower it while you play to hear the sound grow darker.", edited ("character")));
        l.steps.push_back (step ("drive", "osp.macro.drive", "DRIVE",
                                 "DRIVE saturates the whole sound, from warmth to breakup. At 0 % it is out of the signal path. Raise it slowly.", edited ("drive")));
        auto open = step ("movement", "osp.macro.movement.label", "MOVEMENT",
                          "Click MOVEMENT to choose how the sound changes over time: DRIFT, TAPE, CHORUS, PULSE or SHAPER.",
                          Completion::action ({ "macro.3.opened" }, Condition::state ("osp.popup.movement.open"))
                              .withAlreadyDone (Condition::state ("osp.popup.movement.open"), "Already open"));
        open.revealOnEnter = false;
        l.steps.push_back (open);
        auto mode = step ("movement.mode", "osp.movement.mode", "Choose a movement", "Choose a mode here, then turn the MOVEMENT knob to set how much of it you hear.",
                          edited ("movement.mode"));
        mode.unavailableBody = "MOVEMENT's settings are closed. Show me opens them again.";
        l.steps.push_back (mode);
        l.steps.push_back (step ("echo", "osp.macro.echo", "ECHO",
                                 "ECHO adds repeats of the sound; the knob sets how loud they are. Click ECHO for TIME, FEEDBACK and its type: TAPE, BBD or ASCEND.", edited ("echo")));
        l.steps.push_back (step ("space", "osp.macro.space", "SPACE",
                                 "SPACE places the sound in a reverb; the knob sets how much. Click SPACE to choose ROOM, HALL, PLATE, SPRING and more.", edited ("space")));
        l.steps.push_back (step ("compare", "osp.macros", "Compare", "Turn the effects down one at a time to hear what each one adds. None of them changes the recording itself."));
        l.completionText = "These controls shape the sound after it is played. Each name opens more settings; a double-click on DRIVE, FILTER, MOVEMENT, ECHO or SPACE opens its full rack.";
        setUp (l, { soundReady() }, "These controls shape any sound: use your current one, or the bowed example.");
        c.lessons.push_back (l);
    }
    {
        Lesson l;
        l.id = "osp.presets";
        l.title = "Presets and instruments";
        l.description = "Keep a sound, and move it to another computer.";
        l.minutes = 3;
        l.exampleId = "bowed";
        auto open = step ("library", "osp.header.preset", "Open the Library",
                          juce::String::fromUTF8 ("Click the preset name and choose Open Library\xe2\x80\xa6, or press Cmd+L (Ctrl+L). The Library holds your presets, templates and sounds."),
                          Completion::action ({ "library.opened" }, Condition::state ("osp.library.open")).withAlreadyDone (Condition::state ("osp.library.open"), "Already open"));
        open.revealOnEnter = false;
        l.steps.push_back (open);
        auto preset = step ("preset", "osp.library", "What a preset keeps",
                            "A preset keeps the instrument's settings and which recordings it uses. The recordings stay in OSP's sample store on this computer.");
        preset.unavailableBody = "The Library is closed. Show me opens it again.";
        l.steps.push_back (preset);
        auto save = step ("save", "osp.library.save", "Save the sound",
                          juce::String::fromUTF8 ("Click Save\xe2\x80\xa6 to keep the current sound in the Library under a name of your choice."),
                          Completion::action ({ "osp.library.save.touched" }, Condition::state ("osp.library.open")));
        save.unavailableBody = preset.unavailableBody;
        l.steps.push_back (save);
        l.steps.push_back (step ("export", "osp.header.menu", "Take it elsewhere",
                                 juce::String::fromUTF8 ("To move a sound to another computer, choose Export instrument\xe2\x80\xa6 in the \xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2 menu. It writes one .ospinstrument file holding the settings and copies of the recordings.")));
        l.steps.push_back (step ("open", "osp.header.menu", "Open it again",
                                 juce::String::fromUTF8 ("Open instrument file\xe2\x80\xa6 in the same menu brings an exported instrument back, recordings included. The sound you had before can be restored from the preset name's menu.")));
        l.completionText = "Presets keep your settings and point to recordings on this computer. Exported instruments carry the recordings with them.";
        setUp (l, { soundReady() }, "Saving needs a sound to save: use your current one, or the bowed example.");
        c.lessons.push_back (l);
    }

    // Info: what each control does (Toggle Info, then a click on it).
    auto help = [&c] (juce::String target, juce::String title, juce::String body, juce::String lesson = {}) {
        c.help.push_back ({ std::move (target), std::move (title), std::move (body), std::move (lesson) });
    };
    help ("osp.source.card", "Source", "A layer: its recording drawn as a waveform, the notes playing on it as moving heads, and the controls that play it.", "osp.quickstart");
    help ("osp.source.start", "START", "Where in the recording each note begins, 0 to 100 %. In Granular it moves where the grains are taken.", "osp.quickstart");
    help ("osp.source.tune", "TUNE", "Transposes this layer against the notes you play, in semitones (up to 24 either way).", "osp.layers");
    help ("osp.source.pan", "PAN", "Places this layer from left to right. Centre leaves it as it is.");
    help ("osp.source.level", "LEVEL", "How loud this layer is, from silence to +6 dB.", "osp.quickstart");
    help ("osp.source.reimagined", "REIMAGINED", "How far this layer's REIMAGINED mode transforms the recording. At 0 % the layer plays without it. Click the name to choose the mode.",
          "osp.reimagined");
    help ("osp.source.mode", "Playback mode", "One Shot plays the recording from START; Granular builds the note from overlapping grains. The menu also holds PEDAL LOOP for One Shot.",
          "osp.playback");
    help ("osp.granular.position", "POS", "Where in the recording the grains are taken (START is added).", "osp.playback");
    help ("osp.granular.size", "SIZE", "How long each grain lasts, 20 to 400 ms.", "osp.playback");
    help ("osp.granular.density", "DENS", "How many grains start each second, 4 to 40.", "osp.playback");
    help ("osp.granular.spread", "SPREAD", "How far around POS the grains are taken.", "osp.playback");
    help ("osp.source.keyDna", "KEY DNA", "Gives each key its own offsets of the granular settings, fixed when the note starts. Granular playback only.", "osp.keydna");
    help ("osp.source.mute", "M / S", "M mutes this layer; S solos it (only soloed layers are heard). Loading or removing a sound resets both.", "osp.layers");
    help ("osp.source.eq", "EQ", "This layer's own equaliser: high-pass, shelves, a bell and a low-pass, with DAMPEN beside it. Opening it never switches it on.");
    help ("osp.source.notes", "NOTE MAP", "For a recording of several notes (a Note Atlas): the notes OSP found and the keys they play.");
    help ("osp.source.menu", "Layer menu", "Replace the sound, correct its root note, open Samples for a multi-sample sound, or remove the layer.", "osp.multisample");
    help ("osp.source.root", "ROOT", "The pitch OSP believes the recording was made at, shown in the card's header (? when none was found). Correct it in the layer menu.");
    help ("osp.macro.life", "LIFE", "How differently each note is performed: 0 % repeats exactly, 50 % is natural, 100 % is freer. Click the name for PITCH, TONE, ATTACK and TAKES.",
          "osp.life");
    help ("osp.macro.drive", "DRIVE", "Saturation on the whole sound, from warmth to breakup (TUBE, TAPE or CRUNCH). At 0 % it is out of the signal path.", "osp.sound");
    help ("osp.macro.filter", "FILTER", "The voices' filter: the knob sets the cutoff between MIN and MAX. Click the name for its type (LP24, LP12, HP12, BP12, TILT), resonance and envelope.",
          "osp.sound");
    help ("osp.macro.movement", "MOVEMENT", "How the sound changes over time: DRIFT, TAPE, CHORUS, PULSE or SHAPER. The knob sets how much.", "osp.sound");
    help ("osp.macro.echo", "ECHO", "Repeats of the sound (TAPE, BBD or ASCEND). The knob sets how loud they are; 0 % is off.", "osp.sound");
    help ("osp.macro.space", "SPACE", "Reverb: the knob sets how much. Click the name for ROOM, HALL, PLATE, SPRING and the other spaces.", "osp.sound");
    help ("osp.arp.control", "ARP", "Plays the notes you hold as a repeating pattern. The light switches it on; the rest of the key opens its settings.", "osp.arp");
    help ("osp.sub.row", "SUB", "A sub-bass one or two octaves below the keys you hold, added after the effects. The light switches it on; the row opens WAVE, GRIT, LEVEL and its key range.",
          "osp.sub");
    help ("osp.mod.panel", "AMP, ENV, LFO", "AMP shapes each note's volume (A D S R). ENV and LFO 1 / 2 are movements you connect to controls by dragging their tab onto a knob.",
          "osp.modulation");
    help ("osp.mod.halo", "Modulation ring", "The coloured ring shows how far a source moves this control. Drag it to change the depth; right-click for more.", "osp.modulation");
    help ("osp.header.preset", "Presets and Library", "The preset's name: arrows step through presets; the name opens the Library, starting states and the sound before the last load.",
          "osp.presets");
    help ("osp.header.menu", "Menu", "Add layers, presets and instruments (Export instrument carries the recordings), undo, interface size, Advanced and the tutorials.");
    help ("osp.samples", "Samples", "How each file of a multi-sample sound is used: its role, its root note and its velocity layer. A change is yours and is kept.", "osp.multisample");
    help ("osp.advanced", "Advanced", "Tuning, bend range, glide, Poly or Mono, PITCH CHARACTER, how touch changes the sound (DYNAMICS), MPE and the variation seed.");
    help ("osp.header.mix", "MIX", "With two layers: the balance between A and B.", "osp.layers");
    help ("osp.keyboard", "Keyboard", "Plays the instrument with the mouse, as a MIDI keyboard does. Its keys show the notes you play.");
    return c;
}

//==============================================================================
/** The targets (friend of the editor: they point at its own members, found again each time). */
struct OspLearnTargets
{
    static void add (OspAudioProcessorEditor& e, TargetRegistry& t)
    {
        auto& p = e.ospProcessor;
        auto edited = [&e, &p]() -> EngineCard* {
            auto& card = e.engineCard (p.editLayer());
            return card.isVisible() ? &card : nullptr;
        };
        auto comp = [] (juce::Component& c) { return [&c] { return &c; }; };

        // Header and the empty instrument.
        t.add ({ "osp.header.preset", comp (e.presetBar), {}, {}, {}, true });
        t.add ({ "osp.header.menu", comp (e.menuButton), {}, {}, {}, true });
        t.add ({ "osp.header.mix", comp (e.headerMix), {}, {}, [&e] { return e.headerMix.isVisible(); }, true });
        t.add ({ "osp.source.dropZone", comp (e.dropZone), {}, {}, [&e] { return e.dropZone.isVisible(); } });
        t.add ({ "osp.keyboard", comp (e.keyboard), {}, {}, {}, true });
        t.add ({ "osp.advanced", comp (e.advancedButton), {}, [&e] { e.openPopup (OspAudioProcessorEditor::advancedPopup); return e.popup != nullptr; }, {}, true });

        // The edited layer's card and its controls (its letter: whichever layer is edited).
        t.add ({ "osp.source.card", [edited] { return static_cast<juce::Component*> (edited()); }, {}, {}, {}, true });
        const char* knobNames[] = { "start", "tune", "pan", "level", "reimagined" };
        for (int k = 0; k < 5; ++k)
            t.add ({ juce::String ("osp.source.") + knobNames[k], [edited, k] { auto* c = edited(); return c != nullptr ? c->cardKnob (k) : nullptr; }, {}, {}, {}, true });
        t.add ({ "osp.source.reimagined.label", [edited] { return static_cast<juce::Component*> (edited()); },
                 [] (juce::Component& c) { return static_cast<EngineCard&> (c).reimaginedLabelBounds(); } });
        t.add ({ "osp.source.mode", [edited] { auto* c = edited(); return c != nullptr ? static_cast<juce::Component*> (c->modeSelector()) : nullptr; }, {}, {}, {}, true });
        t.add ({ "osp.source.eq", [edited] { auto* c = edited(); return c != nullptr ? static_cast<juce::Component*> (&c->eqToggle()) : nullptr; }, {}, {}, {}, true });
        t.add ({ "osp.source.notes", [edited] { auto* c = edited(); return c != nullptr ? static_cast<juce::Component*> (&c->notesToggle()) : nullptr; }, {}, {},
                 [edited] { auto* c = edited(); return c != nullptr && c->notesToggle().isVisible(); }, true });
        t.add ({ "osp.source.mute", [edited] { auto* c = edited(); return c != nullptr ? &c->muteKey() : nullptr; },
                 [edited] (juce::Component& m) {
                     auto* c = edited();
                     return c != nullptr ? m.getLocalArea (c, c->muteKey().getBounds().getUnion (c->soloKey().getBounds())) : juce::Rectangle<int>();
                 }, {}, {}, true });
        t.add ({ "osp.source.solo", [edited] { auto* c = edited(); return c != nullptr ? &c->soloKey() : nullptr; } });
        t.add ({ "osp.source.menu", [edited] { auto* c = edited(); return c != nullptr ? &c->layerMenuKey() : nullptr; }, {}, {}, {}, true });
        t.add ({ "osp.source.root", [edited] { return static_cast<juce::Component*> (edited()); }, [] (juce::Component& c) {
                     auto r = static_cast<EngineCard&> (c).headingBounds();
                     return r.isEmpty() ? c.getLocalBounds().removeFromTop (34) : r;
                 }, {}, {}, true });
        // Each layer's own (the lessons that compare two layers name them).
        for (int layer = 0; layer < OspAudioProcessor::numLayers; ++layer)
        {
            const auto letter = InstrumentHost::layerName (layer);
            auto card = [&e, layer]() -> EngineCard* { auto& c = e.engineCard (layer); return c.isVisible() ? &c : nullptr; };
            t.add ({ "osp.source." + letter, [card] { return static_cast<juce::Component*> (card()); } });
            for (int k = 0; k < 5; ++k)
                t.add ({ "osp.source." + letter + "." + knobNames[k], [card, k] { auto* c = card(); return c != nullptr ? c->cardKnob (k) : nullptr; } });
            t.add ({ "osp.source." + letter + ".mode", [card] { auto* c = card(); return c != nullptr ? static_cast<juce::Component*> (c->modeSelector()) : nullptr; } });
            t.add ({ "osp.source." + letter + ".solo", [card] { auto* c = card(); return c != nullptr ? &c->soloKey() : nullptr; } });
            t.add ({ "osp.source." + letter + ".mute", [card] { auto* c = card(); return c != nullptr ? &c->muteKey() : nullptr; } });
        }
        // The granular strip: shown on purpose (it is otherwise only there under the pointer).
        auto showStrip = [edited] {
            auto* c = edited();
            if (c == nullptr || ! c->isGranular())
                return false;
            c->showGranularControls (true, true);
            return c->granularControlsShown();
        };
        const char* grainNames[] = { "position", "size", "density", "tune", "spread" };
        for (int k = 0; k < 5; ++k)
            t.add ({ juce::String ("osp.granular.") + grainNames[k], [edited, k] { auto* c = edited(); return c != nullptr ? c->granularKnob (k) : nullptr; }, {}, showStrip,
                     [edited] { auto* c = edited(); return c != nullptr && c->isGranular() && c->granularControlsShown(); }, k != 3 });
        t.add ({ "osp.source.keyDna", [edited] { auto* c = edited(); return c != nullptr ? static_cast<juce::Component*> (&c->keyDnaToggle()) : nullptr; }, {}, showStrip,
                 [edited] { auto* c = edited(); return c != nullptr && c->isGranular() && c->granularControlsShown(); }, true });

        // The macros (data order LIFE DRIVE FILTER MOVEMENT SPACE ECHO): the name and the knob.
        const char* macroNames[] = { "life", "drive", "filter", "movement", "space", "echo" };
        for (int i = 0; i < frame::macroCount; ++i)
        {
            auto& m = e.macros[static_cast<std::size_t> (i)];
            const auto id = juce::String ("osp.macro.") + macroNames[i];
            t.add ({ id, [&m] { return static_cast<juce::Component*> (&m.slider); }, [&e, &m] (juce::Component& knob) {
                         auto r = m.slider.getBounds();
                         if (m.label != nullptr)
                             r = r.getUnion (m.label->getBounds());
                         return knob.getLocalArea (&e.content, r);
                     }, {}, {}, true });
            t.add ({ id + ".label", [&m] { return static_cast<juce::Component*> (m.label.get()); } });
        }
        t.add ({ "osp.macros", comp (e.content), [&e] (juce::Component&) {
                     juce::Rectangle<int> all;
                     for (auto& m : e.macros)
                         all = all.isEmpty() ? m.slider.getBounds() : all.getUnion (m.slider.getBounds());
                     return all;
                 } });
        // Controls inside a popover, found by their parameter (rebuilt with each popover).
        auto inPopup = [&e] (int popup, juce::String parameterId) {
            return Target::Locator ([&e, popup, parameterId] { return e.popupIndex == popup && e.popup != nullptr ? findTagged (e.popup.get(), parameterId) : nullptr; });
        };
        auto openIt = [&e] (int popup) { return std::function<bool()> ([&e, popup] { e.openPopup (popup); return e.popupIndex == popup; }); };
        for (const auto* id : { "life.pitch", "life.tone", "life.attack", "life.takes" })
            t.add ({ juce::String ("osp.") + id, inPopup (0, id), {}, openIt (0) });
        t.add ({ "osp.movement.mode", inPopup (3, "movement.mode"), {}, openIt (3) });
        t.add ({ "osp.reimagined.mode", [&e, &p] {
                     const int popup = OspAudioProcessorEditor::reimaginedPopup + p.editLayer();
                     return e.popupIndex == popup && e.popup != nullptr ? findTagged (e.popup.get(), InstrumentHost::reimaginedModeParameterId (p.editLayer(), "mode")) : nullptr;
                 },
                 {}, [&e, &p] { e.openPopup (OspAudioProcessorEditor::reimaginedPopup + p.editLayer()); return e.popup != nullptr; } });

        // The arpeggiator.
        auto expandArp = [&e] { e.setArpExpanded (true); return e.arpShown; };
        auto panel = [&e] { return static_cast<juce::Component*> (&e.arpPanel); };
        auto ownCard = [&e] { return ! e.arpPanel.showsCards() || e.arpPanel.isLinked() || e.arpPanel.editedCard() == 0; };
        // (ARP is the housing's upper row while SUB has the lower one.)
        t.add ({ "osp.arp.control", comp (e.arpButton),
                 [&e] (juce::Component& c) {
                     auto r = c.getLocalBounds();
                     return e.arpButton.hasSecondRow() ? r.removeFromTop (r.getHeight() / 2) : r;
                 },
                 {}, {}, true });
        // SUB: the housing's lower row, its light, and its panel's controls (by parameter).
        t.add ({ "osp.sub.row", comp (e.arpButton), [&e] (juce::Component&) { return e.arpButton.secondRowArea().getSmallestIntegerContainer(); } });
        t.add ({ "osp.sub.enable", comp (e.arpButton), [&e] (juce::Component&) { return e.arpButton.secondLightArea().expanded (2.0f).getSmallestIntegerContainer(); } });
        auto inSub = [&e] (const juce::String& parameterId) {
            return Target::Locator ([&e, parameterId] {
                return e.popupIndex == OspAudioProcessorEditor::subPopup && e.popup != nullptr ? findTagged (e.popup.get(), parameterId) : nullptr;
            });
        };
        auto openSub = [&e] { e.openPopup (OspAudioProcessorEditor::subPopup); return e.popupIndex == OspAudioProcessorEditor::subPopup; };
        t.add ({ "osp.sub.level", inSub (InstrumentHost::subFieldId ("level")), {}, openSub });
        t.add ({ "osp.sub.range", inSub (InstrumentHost::subFieldId ("lowKey")), {}, openSub });
        t.add ({ "osp.arp.enable", comp (e.arpButton), [&e] (juce::Component&) { return e.arpButton.lightArea().expanded (4.0f).getSmallestIntegerContainer(); } });
        t.add ({ "osp.arp.panel", panel, {}, expandArp });
        t.add ({ "osp.arp.pattern", panel, [&e] (juce::Component&) { return e.arpPanel.patternFieldArea().getUnion (e.arpPanel.patternListArea()).getSmallestIntegerContainer(); },
                 [&e, expandArp] { const bool shown = expandArp(); e.arpPanel.setEditCard (0); return shown; }, ownCard });
        t.add ({ "osp.arp.rate", panel, [&e] (juce::Component&) { return e.arpPanel.shownRateKnob().getBounds(); },
                 [&e, expandArp] { const bool shown = expandArp(); e.arpPanel.setEditCard (0); return shown; }, ownCard });
        t.add ({ "osp.arp.octaves", panel, [&e] (juce::Component&) {
                     auto r = e.arpPanel.octaveKeyArea (1);
                     for (int o = 2; o <= 4; ++o)
                         r = r.getUnion (e.arpPanel.octaveKeyArea (o));
                     return r.getSmallestIntegerContainer();
                 },
                 [&e, expandArp] { const bool shown = expandArp(); e.arpPanel.setEditCard (0); return shown; }, ownCard });
        t.add ({ "osp.arp.gate", [&e] { return findTagged (&e.arpPanel, "arp.gate"); }, {}, expandArp });

        // Modulation: the panel, LFO 1's tab (a click selects it; a drag connects it) and page.
        auto lfoPage = [&e, &p] { e.modPanel.showTab (2); p.setModPanelTab (2); return e.modPanel.tab() == 2; };
        t.add ({ "osp.mod.panel", comp (e.modPanel), {}, {}, {}, true });
        t.add ({ "osp.mod.lfo1", comp (e.modPanel), [&e] (juce::Component&) { return e.modPanel.tabArea (2).getSmallestIntegerContainer(); } });
        t.add ({ "osp.mod.lfo1.shape", [&e] { return e.modPanel.tab() == 2 ? findTagged (&e.modPanel, "mod.lfo1.shape") : nullptr; }, {}, lfoPage });
        t.add ({ "osp.mod.lfo1.rate", [&e] {
                     if (e.modPanel.tab() != 2)
                         return static_cast<juce::Component*> (nullptr);
                     auto* rate = findTagged (&e.modPanel, "mod.lfo1.rate");
                     return rate != nullptr ? rate : findTagged (&e.modPanel, "mod.lfo1.division");
                 },
                 {}, lfoPage });
        // The control LFO 1 moves (its halo): the first route from LFO 1, found by its parameter.
        t.add ({ "osp.mod.halo", [&e, &p]() -> juce::Component* {
                     for (const auto& r : p.modulationRoutes())
                         if (r.route.source == mod::Source::lfo1 && r.state != mod::RouteState::empty && ! r.isDepth())
                             for (auto* root : { static_cast<juce::Component*> (&e.content) })
                                 for (const bool poly : { false, true })
                                 {
                                     // (each control carries its parameter; the halo is drawn around it)
                                     std::function<juce::Component* (juce::Component*)> find = [&] (juce::Component* c) -> juce::Component* {
                                         const auto id = c->getProperties()["paramId"].toString();
                                         if (id.isNotEmpty() && c->isVisible() && modui::destinationFor (id, poly) == r.route.dest)
                                             return c;
                                         for (auto* child : c->getChildren())
                                             if (child->isVisible())
                                                 if (auto* f = find (child))
                                                     return f;
                                         return nullptr;
                                     };
                                     if (auto* found = find (root))
                                         return found;
                                 }
                     return nullptr;
                 },
                 [] (juce::Component& c) { return c.getLocalBounds().expanded (6); }, {}, {}, true });

        // KEY DNA (over the instrument's upper part).
        auto keyDna = [&e] { return static_cast<juce::Component*> (e.keyDnaPanel.get()); };
        auto openKeyDna = [&e, &p] { e.openKeyDna (p.editLayer()); return e.keyDnaPanel != nullptr; };
        auto part = [] (const char* name) {
            return [name] (juce::Component& c) { return static_cast<KeyDnaEditor&> (c).controlArea (name).getSmallestIntegerContainer(); };
        };
        t.add ({ "osp.keydna.graph", keyDna, [] (juce::Component& c) { return static_cast<KeyDnaEditor&> (c).graphArea().getSmallestIntegerContainer(); }, openKeyDna });
        t.add ({ "osp.keydna.power", keyDna, part ("power"), openKeyDna });
        t.add ({ "osp.keydna.depth", keyDna, part ("depth"), openKeyDna });
        t.add ({ "osp.keydna.variation", keyDna, part ("variation"), openKeyDna });
        t.add ({ "osp.keydna.amount", keyDna, part ("amount"), openKeyDna });
        t.add ({ "osp.keydna.tabs", keyDna, [] (juce::Component& c) {
                     auto& k = static_cast<KeyDnaEditor&> (c);
                     juce::Rectangle<float> r;
                     for (int i = 0; i < keydna::paramCount; ++i)
                     {
                         const auto a = k.controlArea ("param." + juce::String (keydna::paramId (static_cast<keydna::Param> (i))));
                         r = r.isEmpty() ? a : r.getUnion (a);
                     }
                     return r.getSmallestIntegerContainer();
                 },
                 openKeyDna });

        // Samples, the Library.
        auto showSamples = [&e, &p] {
            const auto inst = p.currentInstrument (p.editLayer());
            if (inst == nullptr || inst->set == nullptr)
                return false;
            e.samplesShown = true;
            e.samplesPanel.setVisible (true);
            e.samplesPanel.setInstrument (inst, p.editLayer());
            e.updateFocus();
            return true;
        };
        t.add ({ "osp.samples", [&e] { return e.samplesShown ? static_cast<juce::Component*> (&e.samplesPanel) : nullptr; }, {}, showSamples, {}, true });
        for (const auto* which : { "role", "root", "layer" })
            t.add ({ juce::String ("osp.samples.") + which, [&e, which] { return e.samplesShown ? e.samplesPanel.firstRowControl (which) : nullptr; }, {}, showSamples });
        auto openLibrary = [&e] { e.openLibrary(); return e.libraryPanel != nullptr; };
        t.add ({ "osp.library", [&e] { return static_cast<juce::Component*> (e.libraryPanel.get()); }, {}, openLibrary });
        t.add ({ "osp.library.save", [&e] { return e.libraryPanel != nullptr ? &e.libraryPanel->saveKey() : nullptr; }, {}, openLibrary, {}, false, true });

        // The states lessons check (message thread, read each time).
        auto playable = [&p] (int layer) { return p.loadState (layer) == InstrumentHost::LoadState::ready && p.currentInstrument (layer) != nullptr; };
        auto playableCount = [&p, playable] {
            int n = 0;
            for (int l = 0; l < OspAudioProcessor::numLayers; ++l)
                n += playable (l) ? 1 : 0;
            return n;
        };
        t.addState ("osp.source.ready", [&p, playable] { return playable (p.editLayer()); });
        t.addState ("osp.source.empty", [&p] { return p.occupiedLayerCount() == 0; });
        t.addState ("osp.layers.two", [playableCount] { return playableCount() >= 2; });
        t.addState ("osp.layers.fewerThanTwo", [playableCount] { return playableCount() < 2; });
        t.addState ("osp.source.set", [&p, playable] {
            const auto inst = p.currentInstrument (p.editLayer());
            return playable (p.editLayer()) && inst->set != nullptr && inst->atlas == nullptr && inst->set->members.size() > 1;
        });
        t.addState ("osp.source.granular", [&e, &p] { return e.engineCard (p.editLayer()).isGranular(); });
        t.addState ("osp.popup.reimagined.open", [&e, &p] { return e.popupIndex == OspAudioProcessorEditor::reimaginedPopup + p.editLayer(); });
        t.addState ("osp.popup.life.open", [&e] { return e.popupIndex == 0; });
        t.addState ("osp.popup.movement.open", [&e] { return e.popupIndex == 3; });
        t.addState ("osp.keydna.open", [&e] { return e.keyDnaPanel != nullptr; });
        t.addState ("osp.keydna.enabled", [&p] { return p.keyDnaSettings (p.editLayer()).enabled; });
        t.addState ("osp.arp.panel.open", [&e] { return e.arpShown; });
        t.addState ("osp.popup.sub.open", [&e] { return e.popupIndex == OspAudioProcessorEditor::subPopup; });
        t.addState ("osp.mod.tab.lfo1", [&e] { return e.modPanel.tab() == 2; });
        t.addState ("osp.mod.lfo1.routed", [&p] {
            for (const auto& r : p.modulationRoutes())
                if (r.route.source == mod::Source::lfo1 && r.state != mod::RouteState::empty && ! r.isDepth())
                    return true;
            return false;
        });
        t.addState ("osp.samples.open", [&e] { return e.samplesShown; });
        t.addState ("osp.library.open", [&e] { return e.libraryPanel != nullptr; });
    }
};

//==============================================================================
void OspAudioProcessorEditor::createLearn()
{
    andor::learn::EditorHooks hooks;
    hooks.content = &content;
    hooks.menuAnchor = [this] { return menuButton.getBounds(); };
    hooks.closePopovers = [this] { closePopup(); };
    hooks.showing = [this] { return isShowing(); };
    // "layer{L}" in a lesson is the layer being edited (REIMAGINED A keeps its original ID).
    auto resolve = [this] (const juce::String& id) {
        const int layer = ospProcessor.editLayer();
        if (id == reimaginedToken)
            return InstrumentHost::reimaginedParameterId (layer);
        return id.replace (editedLayerToken, "layer" + InstrumentHost::layerName (layer));
    };
    hooks.parameterValue = [this, resolve] (const juce::String& id) -> std::optional<float> {
        if (auto* p = ospProcessor.parameters.getParameter (resolve (id)))
            return p->convertFrom0to1 (p->getValue());
        return std::nullopt;
    };
    hooks.hasCapability = [] (const juce::String&) { return true; };
    // The examples: only on the musician's choice, after the whole state was kept.
    hooks.loadExample = [this] (const juce::String& id) {
        const bool ok = ospProcessor.loadTutorialExample (id);
        timerCallback();
        return ok;
    };
    hooks.canRestoreSound = [this] { return ospProcessor.canRestoreBeforeTutorial(); };
    hooks.restoreSound = [this] {
        const bool ok = ospProcessor.restorePreviousState();
        presetBar.refresh();
        timerCallback();
        return ok;
    };
    learn = std::make_unique<andor::learn::LearnController> (ospCatalog(), std::move (hooks));
    OspLearnTargets::add (*this, learn->targets());
    learn->finishSetup();
    if (! learn->isUsable())
        return;

    // The musician's parameter edits: a gesture of the editor's own controls (never automation,
    // a preset or undo). The edited layer's are also reported as "layer{L}...".
    juce::Component::SafePointer<OspAudioProcessorEditor> safe (this);
    learnGestures = std::make_unique<andor::learn::ParameterGestureBridge> (
        ospProcessor,
        [safe] (const andor::learn::LearnEvent& event) {
            if (safe == nullptr || safe->learn == nullptr)
                return;
            safe->learn->userAction (event);
            const int layer = safe->ospProcessor.editLayer();
            const auto prefix = "layer" + InstrumentHost::layerName (layer) + ".";
            juce::String alias;
            if (event.parameterId == InstrumentHost::reimaginedParameterId (layer))
                alias = reimaginedToken;
            else if (event.parameterId.startsWith (prefix))
                alias = editedLayerToken + "." + event.parameterId.substring (prefix.length());
            if (alias.isNotEmpty())
            {
                auto e = event;
                e.actionId = "param:" + alias;
                safe->learn->userAction (e);
            }
        },
        [safe] (bool held) {
            if (safe != nullptr && safe->learn != nullptr)
                safe->learn->runner().setGestureActive (held);
        });

    // The clicks that open things (from their own handlers: a reveal never reports one).
    for (int layer = 0; layer < OspAudioProcessor::numLayers; ++layer)
    {
        auto& card = *cards[static_cast<std::size_t> (layer)];
        auto keyDna = card.onKeyDna;
        card.onKeyDna = [this, keyDna] (int l) {
            keyDna (l);
            learnAction ("keydna.opened", "osp.source.keyDna");
        };
        auto reimagined = card.onReimagined;
        card.onReimagined = [this, reimagined] (int l) {
            reimagined (l);
            if (popupIndex == reimaginedPopup + l)
                learnAction ("reimagined.opened", "osp.source.reimagined.label");
        };
    }
    for (int i = 0; i < frame::macroCount; ++i)
        if (auto* label = macros[static_cast<std::size_t> (i)].label.get())
        {
            auto click = label->onClick;
            label->onClick = [this, click, i] {
                if (click != nullptr)
                    click();
                if (popupIndex == popupOfMacro (i))
                    learnAction ("macro." + juce::String (i) + ".opened", {});
            };
        }
    auto toggle = arpButton.onToggleEditor;
    arpButton.onToggleEditor = [this, toggle] {
        toggle();
        learnAction (arpShown ? andor::learn::actions::arpPanelOpened : andor::learn::actions::arpPanelClosed, "osp.arp.control");
    };
    auto subRow = arpButton.onSecondRow;
    arpButton.onSecondRow = [this, subRow] {
        if (subRow != nullptr)
            subRow();
        if (popupIndex == subPopup)
            learnAction ("sub.opened", "osp.sub.row");
    };
    arpButton.onLearnAction = [this] (const juce::String& action, const juce::String& parameterId) {
        if (learn != nullptr)
            learn->userAction (action, "osp.arp.enable", parameterId, andor::learn::Origin::userPointer);
    };
    arpPanel.onLearnAction = [this] (const juce::String& action, const juce::String& parameterId) {
        if (learn != nullptr)
            learn->userAction (action, "osp.arp.panel", parameterId, andor::learn::Origin::userPointer);
    };
    auto tabChanged = modPanel.onTabChanged;
    modPanel.onTabChanged = [this, tabChanged] (int tab) {
        tabChanged (tab);
        if (tab == 2)
            learnAction ("mod.tab.lfo1", "osp.mod.lfo1");
    };
    modPanel.onDragEnd = [this] (int source, juce::Point<int> screen) {
        if (dropModulation (source, content.getLocalPoint (nullptr, screen).toFloat()) >= 0)
            learnAction ("mod.route.created", "osp.mod.panel");
    };
    auto openLib = presetBar.onOpenLibrary;
    presetBar.onOpenLibrary = [this, openLib] {
        openLib();
        learnAction ("library.opened", "osp.library");
    };
    auto browse = dropZone.onBrowse;
    dropZone.onBrowse = [browse] { browse(); };   // (the load is reported when the files are chosen)
    auto example = dropZone.onExample;
    dropZone.onExample = [this, example] {
        example();
        learnAction ("source.load.requested", "osp.source.dropZone");
    };
    samplesPanel.onUserEdit = [this] (const juce::String& what) { learnAction ("samples." + what, "osp.samples"); };
}

void OspAudioProcessorEditor::learnAction (const juce::String& actionId, const juce::String& targetId)
{
    // Called only from the handlers of the musician's own clicks, drops and keys.
    if (learn != nullptr)
        learn->userAction (actionId, targetId, {}, andor::learn::Origin::userPointer);
}

void OspAudioProcessorEditor::tickLearn()
{
    if (learn == nullptr)
        return;
    // A note played (the musician's keys or MIDI keyboard, counted by the processor).
    const int notes = ospProcessor.playedVelocityCount();
    if (learnNotesSeen >= 0 && notes != learnNotesSeen)
        learn->userAction ("note.played", "osp.keyboard", {}, andor::learn::Origin::userKeyboard);
    learnNotesSeen = notes;
    // KEY DNA's own edits (its window is made when it opens).
    if (keyDnaPanel != nullptr && keyDnaPanel->onUserEdit == nullptr)
        keyDnaPanel->onUserEdit = [this] (const juce::String& what) { learnAction ("keydna." + what, "osp.keydna.graph"); };
    // A step on a granular control keeps the strip showing (it otherwise follows the pointer).
    if (learn->noteKind() == andor::learn::LearnController::NoteKind::lesson)
        if (const auto* s = learn->runner().step(); s != nullptr && (s->targetId.startsWith ("osp.granular.") || s->targetId == "osp.source.keyDna"))
        {
            auto& card = engineCard (ospProcessor.editLayer());
            if (card.isVisible() && card.isGranular() && ! card.granularControlsShown())
                card.showGranularControls (true, true);
        }
    learn->tick();
}

} // namespace osp::plugin
