#pragma once

#include "LoadedInstrument.h"

#include "analysis/phrase/PhraseAnalysis.h"
#include "engine/Arpeggiator.h"
#include "engine/InstrumentEngine.h"
#include "engine/KeyDna.h"
#include "engine/Modulation.h"
#include "engine/RhythmicShaper.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace osp::plugin
{

/**
    What the shared ANDOR interface (shared/andor-ui) needs from the instrument it shows: the
    parameter state, the A/B/C sources, presets, modulation and a few live displays. ANDOR OSP's
    processor implements it (OspAudioProcessor); a sister product's processor implements it too
    (apps/andor-shell: andor::shell::ShellProcessor), so both show the very same components.

    The parameter-ID scheme (layerParameterId, modRouteId, ...) and the shared value types live
    here unchanged from ANDOR OSP: the components bind to these IDs, so a product that wants a
    component defines the parameter it binds to under the same ID.

    Threads: every function is for the message thread unless it says otherwise.
*/
class InstrumentHost
{
public:
    virtual ~InstrumentHost() = default;

    // -- Parameter state ---------------------------------------------------------------------
    virtual juce::AudioProcessorValueTreeState& apvts() = 0;
    virtual const juce::AudioProcessorValueTreeState& apvts() const = 0;
    virtual juce::UndoManager& undo() = 0;
    /** A parameter's value in its own units (0 for an unknown ID). */
    float parameterValue (const juce::String& id) const;
    /** Sets a parameter (its own units), notifying the host. */
    void setParameterValue (const juce::String& id, float value);
    /** The audio sample rate (EQ display); 0 before prepareToPlay. */
    virtual double hostSampleRate() const = 0;

    // -- The parameter-ID scheme (ANDOR OSP's; unchanged) ------------------------------------
    static constexpr int numLayers = EngineSettings::layers;   ///< A, B, C
    static juce::String layerName (int layer) { return layer == 2 ? "C" : (layer == 1 ? "B" : "A"); }
    /** The stable parameter ID of a layer setting, e.g. layerParameterId (1, "sourceMode") -> "layerB.sourceMode". */
    static juce::String layerParameterId (int layer, const juce::String& name) { return "layer" + layerName (layer) + "." + name; }
    /** A layer's Original <-> Reimagined: A's is the instrument's `reimagined` (the ID older
        sessions and automation know); B and C have their own. */
    static juce::String reimaginedParameterId (int layer) { return layer <= 0 ? juce::String ("reimagined") : layerParameterId (layer, "reimagined"); }
    /** REIMAGINED modes: a layer's mode and every mode's own settings, "layerA.reimagined.mode", ... */
    static juce::String reimaginedModeParameterId (int layer, const juce::String& name) { return layerParameterId (layer, "reimagined." + name); }
    static juce::String modLfoId (int lfo, const juce::String& name);
    static juce::String modEnvId (int env, const juce::String& name);
    static juce::String modRouteId (int route, const juce::String& name);
    /** The modulation destinations' names, in the registry's order (append only: sessions store the index). */
    static const juce::StringArray& modDestinationNames();
    /** The second destination parameter's choices (mod.routeN.destExt): "None", then
        mod::extendedDests (KEY DNA AMOUNT A-C, MIX, MIX TOWARDS C). */
    static const juce::StringArray& modExtendedDestinationNames();
    /** A route slot's destination, from both of its parameters (the second wins when set). */
    mod::Dest routeDest (int slot) const;
    /** Sets a route slot's destination through whichever parameter holds it. */
    void setRouteDest (int slot, mod::Dest dest);
    /** The editable curves: 0, 1 LFO 1/2 CUSTOM; 2, 3 ENV 1/2 one-shot; 4 VEL; 5 NOTE;
        6, 7, 8 DAMPEN A/B/C (over the keys). */
    static constexpr int modCurveCount = 9;
    using ModCurves = std::array<mod::Curve, modCurveCount>;
    static ModCurves defaultModulationCurves();
    /** "c3:" + nine curves; "c2:" (six: before DAMPEN) and "c1:" (four: before VEL and NOTE)
        still read, the curves they lack at their defaults. */
    static juce::String encodeModulationCurves (const ModCurves& curves);
    static std::optional<ModCurves> decodeModulationCurves (const juce::String& text);
    static constexpr int dampenCurveIndex (int layer) noexcept { return 6 + std::clamp (layer, 0, numLayers - 1); }
    static juce::String dampenParameterId (int layer, const juce::String& name) { return layerParameterId (layer, "dampen." + name); }
    /** DAMPEN's parameters in their order: enabled, mode, amount, frequency, cutoff. */
    static const juce::StringArray& dampenParameterNames();
    static const juce::StringArray& eqParameterNames();   ///< enabled, hp.enabled, hp.frequency, ... in order
    static juce::String eqParameterId (int layer, const juce::String& name) { return layerParameterId (layer, "eq." + name); }
    static const char* eqBandKey (eq::Band band) noexcept;   ///< "hp", "lowShelf", "bell", "highShelf", "lp"
    /** The grain budget's choices (index -> grains at once; 0 = Unlimited, the original), never
        reorder: sessions store the index. New sessions start at 256 (index 3, chosen from the
        Phase 2 benchmark); sessions saved before state version 13 open Unlimited. */
    static constexpr std::array<int, 8> grainBudgetChoices { 0, 512, 384, 256, 192, 128, 96, 64 };
    static constexpr int newSessionGrainBudgetIndex = 3;
    static int grainBudgetGrains (float index) noexcept
    {
        return grainBudgetChoices[static_cast<std::size_t> (std::clamp (static_cast<int> (std::lround (index)), 0, 7))];
    }
    /** The arpeggiator's choices, in parameter order (never reorder: sessions store indices). */
    static const juce::StringArray& arpPatternNames();
    static const juce::StringArray& arpRateNames();

    /** The whole ANDOR parameter schema (ANDOR OSP's, frozen; AndorParameters.h has the IDs):
        every product that shows the shared components builds its parameters with it. */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** A layer's EQ as its parameters set it now (from parameterValue). */
    eq::Settings layerEqSettings (int layer) const;
    /** The EQ's settings from its parameter values (eqParameterNames() order). */
    static eq::Settings eqFromValues (const std::array<float, 19>& values);

    // -- A/B/C sources -----------------------------------------------------------------------
    enum class LoadState { empty, loading, ready, failed };
    /** `layer` -1: the layer being edited. */
    virtual LoadState loadState (int layer = -1) const noexcept = 0;
    /** The source a layer shows (waveform, root, file), or nullptr when it is empty (any non-audio thread). */
    virtual std::shared_ptr<const LoadedInstrument> currentInstrument (int layer = -1) const = 0;
    /** Manual root (fractional MIDI), or nullopt for the analysed one. */
    virtual std::optional<double> rootOverride (int layer = -1) const = 0;
    /** Heard in the mix: SOLO wins (only soloed layers are heard), else MUTE decides. */
    bool isLayerHeard (int layer) const;
    /** SOLO this layer alone (alt-click on S). */
    void soloOnly (int layer);
    /** What a layer's voices are doing, for its display (lock-free, any thread). */
    virtual const InstrumentEngine::GrainSnapshot& grainSnapshot (int layer) const noexcept = 0;
    /** The layer holds or loads a sound. */
    virtual bool isLayerOccupied (int layer) const = 0;
    /** A layer whose sound the instrument makes itself (a synthesizer's oscillators) rather
        than a loaded recording: its card shows `name` where a file's state would be, and
        `cycle` (one period of the waveform, -1..1) instead of the recording. `revision`
        changes whenever the picture does. ANDOR OSP has none: every layer plays a recording. */
    struct GeneratedSource
    {
        juce::String name;
        std::vector<float> cycle;
        std::uint64_t revision = 0;
    };
    /** Message thread. False (the default): the layer plays recordings. */
    virtual bool generatedSource (int layer, GeneratedSource& out) const
    {
        juce::ignoreUnused (layer, out);
        return false;
    }
    int occupiedLayerCount() const;
    /** A layer the musician just added is heard at once: two layers meet in the middle of the
        blend; a third added to a pair the musician set (`keepMix`) takes a third of the mix,
        A and B keeping their balance; three together meet at the centre. Message thread. */
    void makeLayerAudible (int layer, bool keepMix = false);
    /** Layer control names for layerParameterId: start, tune, pan, level, link, reverse, loop, follow. */
    static const juce::StringArray& layerControlNames();
    /**
        LINK: a user change of one layer's START, TUNE, PAN or LEVEL (`control`) by `delta`
        moves every other linked layer's same control by the same amount (clamped), so
        their relationship is kept. Nothing happens unless `layer` itself is linked.
        Message thread (a UI gesture; automation is never propagated).
    */
    void applyLinkedDelta (int layer, const juce::String& control, float delta);
    bool isLayerLinked (int layer) const { return parameterValue (layerParameterId (layer, "link")) >= 0.5f; }

    // -- Note Atlas sources and KEY DNA (unified engine Phase 5) ------------------------------
    // A product without them keeps the defaults: nothing to map, nothing to edit, and the
    // interface shows none of their controls.

    /** What a layer is doing with its Note Atlas, for the card's quiet status line. */
    enum class AtlasActivity { none, analysing, preparing, enhancing };
    /** The layer holds one recording that MAP NOTES can turn into an instrument of its notes. */
    virtual bool canMapNotes (int layer) const
    {
        juce::ignoreUnused (layer);
        return false;
    }
    /**
        MAP NOTES: the layer's recording is analysed (in the background) into a Note Atlas and
        played from it, playable as soon as Stage A is built. On a layer that already holds an
        analysed atlas it detects again with `detection` and keeps the musician's corrections
        (one undo step). Message thread.
    */
    virtual void mapNotes (int layer, phrase::Detection detection = phrase::Detection::normal)
    {
        juce::ignoreUnused (layer, detection);
    }
    /** "Map notes?" (Phase 5 review): a recording the musician just dropped on the layer that
        holds several notes, as its ordinary load heard them, so the card asks at once whether
        to map them. 0: nothing to ask (one note, a recalled session, an answered prompt). */
    virtual int mapNotesPrompt (int layer) const
    {
        juce::ignoreUnused (layer);
        return 0;
    }
    /** The prompt answered "no" (or made moot): it is not asked again for this load. */
    virtual void dismissMapNotesPrompt (int layer) { juce::ignoreUnused (layer); }
    /** An edit of the layer's atlas, applied on a background thread (where the recording's
        audio is at hand: splits and merges re-describe the notes) and published like a load:
        playable at once, finished in the background, one undo step named `name`. The edit
        returns false when it changes nothing (nothing is published). Message thread. */
    using AtlasEdit = std::function<bool (atlas::NoteAtlas&, const AudioData& source)>;
    virtual void editAtlas (int layer, const juce::String& name, AtlasEdit edit)
    {
        juce::ignoreUnused (layer, name, edit);
    }
    virtual AtlasActivity atlasActivity (int layer) const noexcept
    {
        juce::ignoreUnused (layer);
        return AtlasActivity::none;
    }
    /** The atlas a layer plays (null: an ordinary sample, or none). */
    std::shared_ptr<const atlas::NoteAtlas> layerAtlas (int layer) const
    {
        const auto instrument = currentInstrument (layer);
        return instrument != nullptr ? instrument->atlas : nullptr;
    }
    /** The DETECTION an atlas's notes came from (its analysis settings), normal when unknown. */
    static phrase::Detection detectionOf (const atlas::NoteAtlas& atlas);
    /** Plays frames [start, end) of the layer's atlas source as recorded (NOTE MAP's AUDITION),
        outside the instrument and its effects. Message thread. */
    virtual void auditionAtlasRegion (int layer, std::int64_t start, std::int64_t end)
    {
        juce::ignoreUnused (layer, start, end);
    }

    /** The last re-detection on the layer (DETECTION): what it kept and where it disagrees with
        the musician's corrections (note ids, kept as the musician left them). Empty: none. */
    virtual juce::String atlasReport (int layer) const
    {
        juce::ignoreUnused (layer);
        return {};
    }
    virtual std::vector<std::string> atlasConflicts (int layer) const
    {
        juce::ignoreUnused (layer);
        return {};
    }
    /** The musician has looked at a conflict and keeps their note: it is no longer shown. */
    virtual void dismissAtlasConflict (int layer, const std::string& noteId)
    {
        juce::ignoreUnused (layer, noteId);
    }

    /** A product with KEY DNA (OSP's Granular). The KEY DNA editor shows only when true. */
    virtual bool hasKeyDna() const noexcept { return false; }
    virtual keydna::Settings keyDnaSettings (int layer) const
    {
        juce::ignoreUnused (layer);
        return {};
    }
    /** New settings: the profile is computed off the audio thread and published (AMOUNT, a
        host parameter, is not part of it). `undoable`: one undo step named `name`. */
    virtual void setKeyDnaSettings (int layer, const keydna::Settings& settings, const juce::String& name, bool undoable)
    {
        juce::ignoreUnused (layer, settings, name, undoable);
    }
    /** A parameter's drawn curve (its points; the settings hold the compiled table). A flat
        two-point curve when none was drawn. */
    virtual mod::Curve keyDnaCurve (int layer, keydna::Param param) const;
    virtual void setKeyDnaCurve (int layer, keydna::Param param, const mod::Curve& curve, bool undoable)
    {
        juce::ignoreUnused (layer, param, curve, undoable);
    }
    /** The layer's KEY DNA AMOUNT parameter ("layerA.keyDna.amount"). */
    static juce::String keyDnaAmountId (int layer) { return layerParameterId (layer, "keyDna.amount"); }

    // -- Presets (header) --------------------------------------------------------------------
    struct PresetEntry
    {
        juce::String name;
        int program = -1;         ///< a starting state, or -1
        juce::File file;          ///< a preset file, or a user starting state (startingState)
        bool startingState = false;
    };
    virtual juce::String presetDisplayName() const = 0;
    virtual void stepPresetList (int delta) = 0;
    virtual std::vector<PresetEntry> presetList() const = 0;
    virtual void openPresetEntry (const PresetEntry& entry) = 0;
    virtual bool isFavourite() const = 0;
    virtual void toggleFavourite() = 0;
    /** INIT: an empty patch. */
    virtual void initPatch() = 0;
    /** Every setting to its default; the sounds stay. */
    virtual void resetSettings() = 0;
    virtual bool canRestorePreviousState() const noexcept = 0;
    virtual juce::String previousStateDescription() const = 0;
    virtual bool restorePreviousState() = 0;

    // -- Modulation --------------------------------------------------------------------------
    virtual mod::Curve modulationCurve (int index) const = 0;
    virtual void setModulationCurve (int index, const mod::Curve& curve, bool undoable = true) = 0;
    // The routes live in the parameters (mod.routeN.*) and their links (depth routes'
    // targets, by stable ID) in the state tree's ModLinks child: one implementation for every
    // product (moved unchanged from ANDOR OSP).
    struct ModRouteInfo
    {
        int slot = -1;
        mod::Route route;   ///< a depth route: route.target is its target's slot
        mod::RouteState state = mod::RouteState::empty;
        bool isDepth() const noexcept { return mod::isDepthRoute (route); }
    };
    std::vector<ModRouteInfo> modulationRoutes() const;
    /** The modulation settings as the parameters say now (the rules and loop checks use them). */
    mod::Settings modulationProbe() const;
    struct ModAssignResult
    {
        int slot = -1;
        bool existed = false;
        juce::String error;
    };
    /** A route from `source` to `dest` in the first free slot (or the slot already joining
        them); returns the slot, or -1 when all 16 are taken. One undo step. */
    int addModulationRoute (mod::Source source, mod::Dest dest, float depthPercent);
    /** THE assignment path (dragging a source, right-click "Assign", the routes popover):
        checks the rules (scope, timing, source), loops and capacity; an existing route
        joining the two is selected, never duplicated and its depth kept; a new one starts at
        `depthPercent` (one undo step). `error` says why nothing was made. */
    ModAssignResult assignModulation (mod::Source source, mod::Dest dest, float depthPercent = 50.0f);
    /** META-MODULATION through the same path: `source` moves the depth of the route in
        `targetSlot` (one level only, never a source on its own route's depth). */
    ModAssignResult assignDepthModulation (mod::Source source, int targetSlot, float depthPercent = 20.0f);
    /** Removes a route; the routes moving its depth go with it, in the same undo step. */
    void removeModulationRoute (int slot, bool ownUndoStep = true);
    /** Why `source` cannot move that route's depth (empty: it can). */
    juce::String depthModulationRefusal (mod::Source source, int targetSlot) const;
    bool canModulateDepth (mod::Source source, int targetSlot) const { return depthModulationRefusal (source, targetSlot).isEmpty(); }
    /** Whether `source` may drive `dest` with the sources' current scopes (a per-voice
        source never reaches a shared stage). */
    bool canModulate (mod::Source source, mod::Dest dest) const;
    /** A route's stable ID (saved with the session; 0 until it needs one), a depth route's
        target slot (-1: none), and the depth routes moving a route. */
    int modRouteUid (int slot) const;
    int modRouteTarget (int slot) const;
    std::vector<int> depthRoutesOf (int slot) const;
    /** The state tree's child holding the routes' links ("ModLinks"). */
    static const juce::Identifier& modLinksTreeType();
    /** The sources' live values for the display (lock-free). */
    virtual InstrumentEngine::ModView modulationView() const noexcept = 0;
    /** The mix as it plays (with any MIX modulation) for the header's live node; an engine that
        does not blend layers this way says it is not modulated. */
    virtual InstrumentEngine::LiveMix liveMix() const noexcept { return {}; }
    virtual int modControllerNumber (int slot) const noexcept = 0;
    virtual void setModControllerNumber (int slot, int controller) noexcept = 0;
    virtual void learnModController (int slot) noexcept = 0;
    virtual int learningModController() const noexcept = 0;
    virtual bool velocityAppliesToAmp() const noexcept = 0;
    virtual void setVelocityAppliesToAmp (bool on) noexcept = 0;
    virtual bool arpRateIsStepped() const noexcept = 0;
    virtual void setArpRateStepped (bool stepped) noexcept = 0;

    // -- Live displays (written by the audio thread, read lock-free) -------------------------
    static constexpr int velocityHistory = 16;
    /** The last played velocities, a ring of 16 (newest at playedVelocityCount() - 1). */
    virtual int recentVelocityAt (int index) const noexcept = 0;
    virtual int playedVelocityCount() const noexcept = 0;
    /** The last played note (-1 before the first). */
    virtual int lastPlayedNoteNumber() const noexcept = 0;
    /** What the arpeggiator's 16-step display shows. */
    struct ArpView
    {
        std::array<int, Arpeggiator::displaySteps> low {}, high {};   ///< -1: no note
        int current = -1;
        bool active = false;
    };
    virtual ArpView arpView() const noexcept = 0;

    // -- The macro popovers (MacroPopups.h) --------------------------------------------------
    /** The layer the interface edits (0 A, 1 B, 2 C). Any thread. */
    virtual int editLayer() const noexcept = 0;
    /** The host's tempo in BPM (120 when it gives none): ECHO's synced picture. Any thread. */
    virtual float hostTempo() const noexcept = 0;
    /** MOVEMENT SHAPER's playhead, 0..1 of its bar (lock-free, for the picture). */
    virtual float shaperPhase() const noexcept = 0;
    /** SHAPER's CUSTOM pattern (saved with the session; message thread). An undoable change
        is one undo step. */
    virtual ShaperPattern shaperCustomPattern() const = 0;
    virtual void setShaperCustomPattern (const ShaperPattern& pattern, bool undoable = true) = 0;
    /** The SHAPER editor's magnified view (a view setting saved with the session). */
    virtual bool shaperEditorLarge() const noexcept = 0;
    virtual void setShaperEditorLarge (bool large) noexcept = 0;
    /** A product's own state in words, for its popovers (`topic` is the product's: e.g. a
        derived analysis' progress). Empty (the default): nothing to say. Message thread. */
    virtual juce::String engineStatus (const juce::String& topic) const
    {
        juce::ignoreUnused (topic);
        return {};
    }

    /** A REIMAGINED mode (ReimaginedMode's index) this instrument cannot play on `layer`: its
        popover's picture says so in these words. Empty (the default): the mode plays. */
    virtual juce::String reimaginedModeUnavailable (int layer, int mode) const
    {
        juce::ignoreUnused (layer, mode);
        return {};
    }
    /** Saved SHAPER patterns: one JSON file each in Documents/OSP/Shaper Patterns, shared by
        every ANDOR product. */
    static constexpr const char* shaperPatternExtension = ".ospshaper";
    static juce::File shaperPatternFolder();
    static juce::Array<juce::File> savedShaperPatterns();
    static bool writeShaperPatternFile (const juce::File& file, const ShaperPattern& pattern);
    static std::optional<ShaperPattern> readShaperPatternFile (const juce::File& file);
    /** The pattern as the session stores it ("v1:" + 16 steps), and back. */
    static juce::String encodeShaperPattern (const ShaperPattern& pattern);
    static std::optional<ShaperPattern> decodeShaperPattern (const juce::String& text);

protected:
    juce::ValueTree modLinks() const;
    void setModLink (int slot, int uid, int targetUid);
    int newModRouteUid() const;
    /** A depth route's target slot by identity (the slot holding the target's ID), whatever the parameters say now. */
    int resolveModTarget (int slot) const;
    /** Flushes the parameters into the state tree: a route change and its links are one undo step. */
    void commitRouteChange();
};

} // namespace osp::plugin
