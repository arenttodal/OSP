#include "WheelPopups.h"
#include "AndorParameters.h"
#include "Binding.h"

#include "engine/Vibrato.h"

#include <cmath>

namespace osp::plugin
{

const char* const pitchBendHelp = "The wheel bends notes up or down by RANGE. It springs back to the centre when released.";

std::vector<VibratoSource> defaultVibratoSources()
{
    return { { "A", ids::vibratoSourceA }, { "B", ids::vibratoSourceB }, { "C", ids::vibratoSourceC } };
}

namespace
{
    const juce::Colour wheelAmber { 0xffcd5a1e }, wheelBlue { 0xff4f7290 };

    float raw (const InstrumentHost& host, const juce::String& id, float fallback)
    {
        if (const auto* v = host.apvts().getRawParameterValue (id))
            return v->load (std::memory_order_relaxed);
        return fallback;
    }

    void drawWell (juce::Graphics& g, juce::Rectangle<float> r)
    {
        g.setColour (palette::recessed);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.07f));
        g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    }

    //==========================================================================
    /** The wheel's travel: the bend's reach either side of the note, +-RANGE. */
    class BendPicture final : public juce::Component
    {
    public:
        explicit BendPicture (InstrumentHost& h) : host (h) {}

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawWell (g, r);
            r = r.reduced (10.0f, 8.0f);
            const float range = raw (host, ids::bendRange, 2.0f);
            // The wheel's slot on the left, its centre detent; on the right the pitch it bends
            // to: a ladder of semitones, RANGE above and below lit.
            const float mid = r.getCentreY();
            const auto ladder = r.withTrimmedLeft (24.0f);
            const float maxSt = 24.0f;
            const float perSt = 0.5f * ladder.getHeight() / maxSt;
            g.setColour (palette::textDim.withAlpha (0.25f));
            for (int st = -24; st <= 24; st += 12)
                g.fillRect (ladder.getX(), mid - static_cast<float> (st) * perSt - 0.5f, ladder.getWidth(), 1.0f);
            const float reach = range * perSt;
            g.setColour (wheelAmber.withAlpha (0.22f));
            g.fillRoundedRectangle (ladder.getX(), mid - reach, ladder.getWidth(), std::max (1.5f, 2.0f * reach), 3.0f);
            g.setColour (wheelAmber);
            g.fillRect (ladder.getX(), mid - reach - 1.0f, ladder.getWidth(), 2.0f);
            g.fillRect (ladder.getX(), mid + reach - 1.0f, ladder.getWidth(), 2.0f);
            g.setColour (palette::text.withAlpha (0.55f));
            g.fillRect (ladder.getX(), mid - 0.75f, ladder.getWidth(), 1.5f);
            // The wheel.
            const auto slot = juce::Rectangle<float> (r.getX(), r.getY(), 12.0f, r.getHeight());
            g.setColour (juce::Colour (0xff201a15));
            g.fillRoundedRectangle (slot, 3.0f);
            g.setColour (wheelAmber.withAlpha (0.85f));
            g.fillRoundedRectangle (slot.withSizeKeepingCentre (8.0f, 0.3f * slot.getHeight()), 2.0f);
            g.setFont (fonts::label (9.5f));
            g.setColour (palette::textDim);
            const auto text = range < 0.5f ? juce::String ("no bend") : juce::String::fromUTF8 ("\xc2\xb1") + juce::String (juce::roundToInt (range)) + " st";
            g.drawText (text, ladder.withHeight (12.0f).translated (0.0f, std::max (0.0f, mid - reach - 14.0f - ladder.getY())).reduced (4.0f, 0.0f),
                        juce::Justification::centredRight, false);
        }

    private:
        InstrumentHost& host;
    };

    /** PITCH BEND, a compact popover (the macro popovers' scale, ShapingPopups' popover::):
        RANGE beside the picture of the wheel's reach, one line of explanation. */
    class PitchBendPopup final : public MiniPanel
    {
    public:
        static constexpr int width = 280, pictureHeight = 66, helpHeight = 26;

        explicit PitchBendPopup (InstrumentHost& h) : MiniPanel ("PITCH BEND"), host (h), picture (h)
        {
            range = std::make_unique<MiniKnob> (host.apvts(), ids::bendRange, "RANGE", [] (double v) {
                const int st = juce::roundToInt (v);
                return st == 0 ? juce::String ("0 st") : juce::String::fromUTF8 ("\xc2\xb1") + juce::String (st) + " st";
            });
            range->setArcColour (wheelAmber);
            addAndMakeVisible (*range);
            addAndMakeVisible (picture);
            picture.setInterceptsMouseClicks (false, false);
        }

        bool compact() const override { return true; }
        juce::Point<int> cardSize() const override
        {
            return { width, popover::headerTop + pictureHeight + popover::gap + helpHeight + popover::bottom };
        }
        void refreshContent() override { picture.repaint(); }
        void settleNow() override { picture.repaint(); }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            g.setColour (palette::textDim);
            g.setFont (fonts::label (popover::helpFont));
            g.drawFittedText (pitchBendHelp, help, juce::Justification::topLeft, 2, 1.0f);
        }

    private:
        void layoutContent (juce::Rectangle<int> area) override
        {
            auto row = area.removeFromTop (pictureHeight);
            range->setBounds (row.removeFromLeft (popover::maxCellWidth).withSizeKeepingCentre (popover::maxCellWidth, popover::cellHeight));
            row.removeFromLeft (popover::gap);
            picture.setBounds (row);
            area.removeFromTop (popover::gap);
            help = area.removeFromTop (helpHeight);
        }

        InstrumentHost& host;
        std::unique_ptr<MiniKnob> range;
        BendPicture picture;
        juce::Rectangle<int> help;
    };

    //==========================================================================
    vibrato::Settings readVibrato (const InstrumentHost& host)
    {
        vibrato::Settings v;
        v.enabled = raw (host, ids::vibratoEnabled, 0.0f) >= 0.5f;
        v.depthCents = raw (host, ids::vibratoDepth, 25.0f);
        v.rateHz = raw (host, ids::vibratoRate, 5.5f);
        v.delaySeconds = raw (host, ids::vibratoDelay, 250.0f) / 1000.0;
        v.riseSeconds = raw (host, ids::vibratoRise, 400.0f) / 1000.0;
        v.control = static_cast<vibrato::Control> (std::clamp (juce::roundToInt (raw (host, ids::vibratoControl, 0.0f)), 0, 2));
        v.amount = raw (host, ids::vibratoAmount, 0.0f) / 100.0;
        return v;
    }

    /** One note's pitch over its first 2.5 s, as the settings make it: the dashed outline
        is DEPTH at full, the line what plays now (the wheel's position, ALWAYS ON's all of it,
        or AMOUNT). */
    class VibratoPicture final : public juce::Component
    {
    public:
        VibratoPicture (InstrumentHost& h, std::function<float()> w) : host (h), wheel (std::move (w)) {}

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            drawWell (g, r);
            const auto v = readVibrato (host);
            const double scale = vibrato::scaleFor (v, wheel != nullptr ? static_cast<double> (wheel()) : 0.0);
            auto plot = r.reduced (10.0f, 9.0f).withTrimmedBottom (10.0f);
            const float mid = plot.getCentreY();
            const double span = 2.5;   // seconds shown
            const float perCent = 0.5f * plot.getHeight() / static_cast<float> (vibrato::Settings::maxDepthCents);
            auto xAt = [&] (double t) { return plot.getX() + static_cast<float> (t / span) * plot.getWidth(); };
            g.setColour (palette::text.withAlpha (0.3f));
            g.fillRect (plot.getX(), mid - 0.5f, plot.getWidth(), 1.0f);
            // DELAY and RISE: where the vibrato starts and where it is full.
            g.setColour (palette::textDim.withAlpha (0.35f));
            for (const double t : { v.delaySeconds, v.delaySeconds + v.riseSeconds })
                if (t > 0.0 && t < span)
                    g.fillRect (xAt (t) - 0.5f, plot.getY(), 1.0f, plot.getHeight());
            const auto accent = v.enabled ? wheelBlue : palette::textDim;
            juce::Path outline, line;
            const int steps = 360;
            for (int i = 0; i <= steps; ++i)
            {
                const double t = span * i / steps;
                const double onset = vibrato::onsetAt (v, t);
                const double full = v.depthCents * onset;
                const double now = full * scale * std::sin (2.0 * juce::MathConstants<double>::pi * v.rateHz * t);
                const float x = xAt (t);
                if (i == 0)
                {
                    outline.startNewSubPath (x, mid - static_cast<float> (full) * perCent);
                    line.startNewSubPath (x, mid - static_cast<float> (now) * perCent);
                }
                else
                {
                    outline.lineTo (x, mid - static_cast<float> (full) * perCent);
                    line.lineTo (x, mid - static_cast<float> (now) * perCent);
                }
            }
            for (int i = steps; i >= 0; --i)
            {
                const double t = span * i / steps;
                outline.lineTo (xAt (t), mid + static_cast<float> (v.depthCents * vibrato::onsetAt (v, t)) * perCent);
            }
            g.setColour (accent.withAlpha (0.12f));
            g.fillPath (outline);
            g.setColour (accent.withAlpha (v.enabled ? 1.0f : 0.5f));
            g.strokePath (line, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setFont (fonts::label (9.0f));
            g.setColour (palette::textDim);
            const auto captions = r.reduced (10.0f, 3.0f).removeFromBottom (11.0f);
            g.drawText ("NOTE ON", captions, juce::Justification::centredLeft, false);
            g.drawText ("2.5 s", captions, juce::Justification::centredRight, false);
            juce::String state;
            if (! v.enabled)
                state = "OFF";
            else if (scale <= 0.0)
                state = v.control == vibrato::Control::assigned ? "SILENT UNTIL AMOUNT MOVES" : "WHEEL DOWN: SILENT";
            else
                state = juce::String::fromUTF8 ("\xc2\xb1") + juce::String (juce::roundToInt (v.depthCents * scale)) + " cents";
            g.drawText (state, captions, juce::Justification::centred, false);
        }

    private:
        InstrumentHost& host;
        std::function<float()> wheel;
    };

    /** MOD WHEEL's VIBRATO, a compact popover (the macro popovers' scale, popover::): ON in
        the title row, the picture of one note's pitch, DEPTH .. AMOUNT in one row, CONTROL,
        the sources, one or two lines on what CONTROL does. */
    class ModWheelPopup final : public MiniPanel
    {
    public:
        static constexpr int width = 340, pictureHeight = 64, helpHeight = 24, captionWidth = 58;

        ModWheelPopup (InstrumentHost& h, std::vector<VibratoSource> s, std::function<float()> w)
            : MiniPanel ("MOD WHEEL"), host (h), sourceList (std::move (s)), picture (h, std::move (w))
        {
            auto& state = host.apvts();
            enable = std::make_unique<SegmentedControl> (*state.getParameter (ids::vibratoEnabled), juce::StringArray { "OFF", "ON" });
            enable->setTooltip ("Vibrato: each note's pitch swings gently, starting after DELAY and growing over RISE");
            enable->onChange = [this] (int) { update(); };
            binding::tag (*enable, ids::vibratoEnabled);
            addAndMakeVisible (*enable);
            auto add = [this, &state] (const juce::String& id, const char* caption, MiniKnob::Formatter f) {
                knobs.push_back (std::make_unique<MiniKnob> (state, id, caption, std::move (f)));
                addAndMakeVisible (*knobs.back());
                return knobs.back().get();
            };
            add (ids::vibratoDepth, "DEPTH", [] (double v) { return juce::String (juce::roundToInt (v)) + " ct"; });
            add (ids::vibratoRate, "RATE", [] (double v) { return format::hertz (v); });
            add (ids::vibratoDelay, "DELAY", [] (double v) { return format::milliseconds (v); });
            add (ids::vibratoRise, "RISE", [] (double v) { return format::milliseconds (v); });
            amount = add (ids::vibratoAmount, "AMOUNT", [] (double v) { return format::percent (v) + " %"; });
            control = std::make_unique<SegmentedControl> (*state.getParameter (ids::vibratoControl), juce::StringArray { "MOD WHEEL", "ALWAYS ON", "ASSIGNED" });
            control->setTooltip ("What sets how much vibrato plays: the mod wheel, always all of it, or AMOUNT (for macros, LFOs and envelopes)");
            control->onChange = [this] (int) { update(); };
            binding::tag (*control, ids::vibratoControl);
            addAndMakeVisible (*control);
            for (const auto& source : sourceList)
            {
                auto button = std::make_unique<juce::ToggleButton> (source.label);
                button->setTooltip ("Vibrato on " + source.label);
                if (state.getParameter (source.parameterId) != nullptr)
                    attachments.push_back (std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, source.parameterId, *button));
                binding::tag (*button, source.parameterId);
                addAndMakeVisible (*button);
                sources.push_back (std::move (button));
            }
            addAndMakeVisible (picture);
            picture.setInterceptsMouseClicks (false, false);
            update();
        }

        bool compact() const override { return true; }
        juce::Point<int> cardSize() const override
        {
            using namespace popover;
            return { width, headerTop + pictureHeight + gap + cellHeight + rowGap + tabsHeight + rowGap + tabsHeight + rowGap + helpHeight + bottom };
        }
        void refreshContent() override
        {
            update();
            picture.repaint();
        }
        void settleNow() override { refreshContent(); }

        void paint (juce::Graphics& g) override
        {
            MiniPanel::paint (g);
            g.setColour (palette::textDim);
            g.setFont (fonts::label (popover::captionFont));
            g.drawText ("VIBRATO", enableCaption, juce::Justification::centredRight, false);
            g.drawText ("CONTROL", controlCaption, juce::Justification::centredLeft, false);
            g.drawText ("SOURCES", sourcesCaption, juce::Justification::centredLeft, false);
            g.setFont (fonts::label (popover::helpFont));
            g.drawFittedText (helpText(), help, juce::Justification::topLeft, 2, 1.0f);
        }

        juce::String helpText() const
        {
            switch (juce::roundToInt (raw (host, ids::vibratoControl, 0.0f)))
            {
                case 1: return "Every note gets DEPTH; the mod wheel stays free for its own routes.";
                case 2: return "Silent until something moves AMOUNT: give it to a macro (right-click), or route an LFO or envelope to VIBRATO AMOUNT.";
                default: return "The mod wheel sets how much of DEPTH plays. Each note waits DELAY, then grows over RISE.";
            }
        }

    private:
        void update()
        {
            // AMOUNT is ASSIGNED's own: elsewhere it waits, dimmed (still there to set up).
            const bool assigned = control != nullptr && control->selected() == 2;
            if (amount != nullptr)
                amount->setAlpha (assigned ? 1.0f : 0.45f);
            if (const int c = juce::roundToInt (raw (host, ids::vibratoControl, 0.0f)); c != shownControl)
            {
                shownControl = c;
                repaint (help);
            }
        }

        void layoutContent (juce::Rectangle<int> area) override
        {
            using namespace popover;
            // The title row: VIBRATO OFF / ON on the right, where a macro popover's mode is.
            const auto c = card();
            const auto onOff = juce::Rectangle<int> (c.getRight() - side - 84, c.getY() + 7, 84, 20);
            enable->setBounds (onOff);
            enableCaption = onOff.withX (onOff.getX() - 64).withWidth (58);
            picture.setBounds (area.removeFromTop (pictureHeight));
            area.removeFromTop (gap);
            auto row = area.removeFromTop (cellHeight);
            const int cell = std::min (maxCellWidth, row.getWidth() / static_cast<int> (knobs.size()));
            row = row.withSizeKeepingCentre (cell * static_cast<int> (knobs.size()), row.getHeight());
            for (auto& k : knobs)
                k->setBounds (row.removeFromLeft (cell));
            area.removeFromTop (rowGap);
            auto line = area.removeFromTop (tabsHeight);
            controlCaption = line.removeFromLeft (captionWidth);
            control->setBounds (line);
            area.removeFromTop (rowGap);
            line = area.removeFromTop (tabsHeight);
            sourcesCaption = line.removeFromLeft (captionWidth);
            const int each = sources.empty() ? 0 : std::min (56, line.getWidth() / static_cast<int> (sources.size()));
            for (auto& b : sources)
                b->setBounds (line.removeFromLeft (each));
            area.removeFromTop (rowGap);
            help = area.removeFromTop (helpHeight);
        }

        InstrumentHost& host;
        std::vector<VibratoSource> sourceList;
        VibratoPicture picture;
        std::unique_ptr<SegmentedControl> enable, control;
        std::vector<std::unique_ptr<MiniKnob>> knobs;
        MiniKnob* amount = nullptr;
        std::vector<std::unique_ptr<juce::ToggleButton>> sources;
        std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> attachments;
        juce::Rectangle<int> enableCaption, controlCaption, sourcesCaption, help;
        int shownControl = -1;
    };
}

std::unique_ptr<MiniPanel> createPitchBendPopup (InstrumentHost& host)
{
    return std::make_unique<PitchBendPopup> (host);
}

std::unique_ptr<MiniPanel> createModWheelPopup (InstrumentHost& host, std::vector<VibratoSource> sources, std::function<float()> wheel)
{
    return std::make_unique<ModWheelPopup> (host, std::move (sources), std::move (wheel));
}

} // namespace osp::plugin
