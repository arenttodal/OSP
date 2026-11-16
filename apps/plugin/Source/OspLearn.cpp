#include "OspLearn.h"

namespace osp::plugin
{

using namespace andor::learn;

namespace
{
    juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

    Step explain (const char* id, const char* target, const char* title, const char* body)
    {
        Step s;
        s.id = id;
        s.targetId = target;
        s.title = u8 (title);
        s.body = u8 (body);
        return s;
    }

    HelpTopic h (const char* target, const char* title, const char* body, const char* lesson = "")
    {
        HelpTopic t;
        t.targetId = target;
        t.title = u8 (title);
        t.body = u8 (body);
        t.lessonId = lesson;
        return t;
    }

    Lesson quickStart()
    {
        Lesson l;
        l.id = "osp.quickstart";
        l.title = "Quick Start";
        l.description = "The main parts of ANDOR OSP";
        l.group = "Quick Start";
        l.revision = 1;
        l.minutes = 2;
        l.completionText = u8 ("You\xe2\x80\x99ve seen the main parts of OSP. Point to any control with Info on to read more about it.");
        auto settings = explain ("settings", "keyboard.settings", "Instrument settings",
                                 "Click the Settings icon beneath ARP to open OSP's detailed settings: tuning, voices, pitch character and dynamics.");
        settings.revealOnEnter = false;   // (the key itself: nothing to open first)
        l.steps = {
            explain ("sounds", "header.preset", "Select a sound",
                     "Click the preset name to browse sounds. Use the arrows on either side to move through them."),
            explain ("sources", "sources", "Your sounds",
                     "Drop a tonal recording onto a card, or open the Library from the menu. OSP plays it across the keyboard; up to three recordings layer and blend in the header's MIX."),
            explain ("macros", "macros", "The macros",
                     "LIFE, DYNAMICS, CHARACTER, MOVEMENT, ECHO and SPACE shape the whole instrument. Click a macro's name to open its settings."),
            explain ("reimagined", "source.card.A", "REIMAGINED",
                     "Each card's REIMAGINED turns its recording into a new instrument: granular, tape, toy and other modes. Click REIMAGINED's name on the card for its settings."),
            explain ("modulation", "mod.panel", "Modulation",
                     "LFOs, envelopes, velocity and the mod wheel can move any control. Drag a source's tab onto a control to assign it."),
            explain ("arp", "arp.opener", "Arpeggiator",
                     "ARP plays the notes you hold as a repeating pattern. Click ARP beside the keyboard to open its controls."),
            settings,
        };
        return l;
    }

    std::vector<HelpTopic> help()
    {
        return {
            h ("header.preset", "Presets", "Browse, load and save OSP's sounds. The arrows step through the presets; the name opens the list.", "osp.quickstart"),
            h ("header.menu", "Menu", "Presets, instruments, the Library, undo and redo, the interface size, and Learn's tutorials."),
            h ("source.card.A", "Layer A",
               "A recording played as an instrument. Its controls set the start, tuning, level and its REIMAGINED mode; drop another recording onto it to replace it."),
            h ("source.card.B", "Layer B", "A second recording layered with A. The header's MIX blends the layers."),
            h ("source.card.C", "Layer C", "A third recording layered with A and B. With three layers MIX becomes a triangle."),
            h ("macro.0", "LIFE", "LIFE adds the small variations of a real performance: each note slightly different in timing, pitch and tone. Click its name for its settings."),
            h ("macro.1", "DYNAMICS", "DYNAMICS sets how much your touch (velocity) changes the level and tone. Click its name for its curve and range."),
            h ("macro.2", "CHARACTER", "CHARACTER shapes the tone with OSP's filter, from dark to open. Click its name for its settings."),
            h ("macro.3", "MOVEMENT", "MOVEMENT adds motion: drift, tape wobble, chorus, pulsing or a rhythmic shaper. Click its name to choose."),
            h ("macro.4", "SPACE", "SPACE adds reverberation, from a small room to an endless wash. Click its name for its engine and settings."),
            h ("macro.5", "ECHO", "ECHO repeats the sound. Click its name for its time, feedback and character."),
            h ("macros", "The macros", "Six macros shape the whole instrument. Each name opens its settings."),
            h ("mod.panel", "Modulation",
               "The AMP envelope, two LFOs and two envelopes. Drag a source's tab onto a control to move it automatically; the control's halo shows the route."),
            h ("keyboard", "Keyboard",
               "Play notes here or from a MIDI keyboard. Click PITCH or MOD under the wheels for the bend range and the mod wheel's vibrato."),
            h ("arp.control", "Arpeggiator", "The arpeggiator plays held notes as a repeating pattern. Switch ARP on, or open its panel for the pattern, rate and range."),
            h ("keyboard.settings", "Advanced Settings",
               "Open OSP's detailed instrument settings, including tuning, voice behavior and other supported performance options."),
            h ("keyboard.info", "Info", "Turn Info on to read explanations of OSP's controls as you point to them."),
        };
    }
} // namespace

Catalog ospLearnCatalog()
{
    Catalog c;
    c.productId = "osp";
    c.lessons = { quickStart() };
    c.help = help();
    c.quickStartLessonId = "osp.quickstart";
    return c;
}

} // namespace osp::plugin
