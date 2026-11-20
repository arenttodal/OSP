#include "MidiLearn.h"

#include <algorithm>

namespace osp::plugin
{

namespace
{
    constexpr juce::uint32 gestureIdleMs = 300;   ///< a pause this long ends the change gesture
}

MidiLearn::MidiLearn (juce::AudioProcessor& p, juce::AudioProcessorValueTreeState& s) : processor (p), state (s)
{
    for (auto& t : target)
        t.store (-1, std::memory_order_relaxed);
    for (auto& v : pending)
        v.store (-1, std::memory_order_relaxed);
    gestureParam.fill (-1);
    startTimerHz (60);
}

MidiLearn::~MidiLearn()
{
    stopTimer();
    for (int cc = 0; cc < controllers; ++cc)
        if (auto* open = parameterAt (gestureParam[static_cast<std::size_t> (cc)]))
            open->endChangeGesture();
}

bool MidiLearn::handleController (int number, int value) noexcept
{
    if (number < 0 || number >= controllers)
        return false;
    if (learning.load (std::memory_order_acquire) >= 0)
    {
        heard.store (number, std::memory_order_release);
        pending[static_cast<std::size_t> (number)].store (std::clamp (value, 0, 127), std::memory_order_release);
        return true;
    }
    if (target[static_cast<std::size_t> (number)].load (std::memory_order_relaxed) < 0)
        return false;
    pending[static_cast<std::size_t> (number)].store (std::clamp (value, 0, 127), std::memory_order_release);
    return true;
}

int MidiLearn::indexOf (const juce::String& parameterId) const
{
    if (auto* p = state.getParameter (parameterId))
        return p->getParameterIndex();
    return -1;
}

juce::RangedAudioParameter* MidiLearn::parameterAt (int index) const
{
    const auto& all = processor.getParameters();
    return index >= 0 && index < all.size() ? dynamic_cast<juce::RangedAudioParameter*> (all[index]) : nullptr;
}

void MidiLearn::learn (const juce::String& parameterId)
{
    heard.store (-1, std::memory_order_release);
    learning.store (indexOf (parameterId), std::memory_order_release);
}

juce::String MidiLearn::learningParameter() const
{
    if (auto* p = parameterAt (learning.load (std::memory_order_acquire)))
        return p->getParameterID();
    return {};
}

int MidiLearn::controllerFor (const juce::String& parameterId) const
{
    const int index = indexOf (parameterId);
    if (index < 0)
        return -1;
    for (int cc = 0; cc < controllers; ++cc)
        if (target[static_cast<std::size_t> (cc)].load (std::memory_order_relaxed) == index)
            return cc;
    return -1;
}

void MidiLearn::assign (int number, const juce::String& parameterId)
{
    const int index = indexOf (parameterId);
    if (number < 0 || number >= controllers || index < 0)
        return;
    // One controller turns one control, and a control has one controller.
    for (auto& t : target)
        if (t.load (std::memory_order_relaxed) == index)
            t.store (-1, std::memory_order_relaxed);
    target[static_cast<std::size_t> (number)].store (index, std::memory_order_release);
}

void MidiLearn::forget (const juce::String& parameterId)
{
    const int index = indexOf (parameterId);
    for (auto& t : target)
        if (index >= 0 && t.load (std::memory_order_relaxed) == index)
            t.store (-1, std::memory_order_relaxed);
}

void MidiLearn::clear()
{
    for (auto& t : target)
        t.store (-1, std::memory_order_relaxed);
    cancelLearn();
}

int MidiLearn::mappingCount() const
{
    int n = 0;
    for (const auto& t : target)
        n += t.load (std::memory_order_relaxed) >= 0 ? 1 : 0;
    return n;
}

std::unique_ptr<juce::XmlElement> MidiLearn::toXml() const
{
    if (mappingCount() == 0)
        return nullptr;
    auto xml = std::make_unique<juce::XmlElement> ("MidiLearn");
    xml->setAttribute ("schema", 1);
    for (int cc = 0; cc < controllers; ++cc)
        if (auto* p = parameterAt (target[static_cast<std::size_t> (cc)].load (std::memory_order_relaxed)))
        {
            auto* map = xml->createNewChildElement ("Map");
            map->setAttribute ("cc", cc);
            map->setAttribute ("param", p->getParameterID());
        }
    return xml;
}

void MidiLearn::fromXml (const juce::XmlElement* xml)
{
    clear();
    if (xml == nullptr || xml->getIntAttribute ("schema", 1) > 1)
        return;
    for (auto* map : xml->getChildWithTagNameIterator ("Map"))
        assign (map->getIntAttribute ("cc", -1), map->getStringAttribute ("param"));
}

void MidiLearn::applyPending()
{
    // A controller heard while learning: it now turns the parameter being learned.
    if (const int index = learning.load (std::memory_order_acquire); index >= 0)
        if (const int cc = heard.exchange (-1, std::memory_order_acq_rel); cc >= 0)
        {
            if (auto* p = parameterAt (index))
                assign (cc, p->getParameterID());
            learning.store (-1, std::memory_order_release);
        }

    const auto now = juce::Time::getMillisecondCounter();
    for (int cc = 0; cc < controllers; ++cc)
    {
        const auto i = static_cast<std::size_t> (cc);
        const int value = pending[i].exchange (-1, std::memory_order_acq_rel);
        const int index = target[i].load (std::memory_order_acquire);
        // A gesture left open on a parameter this controller no longer turns (forgotten, moved,
        // or idle long enough) ends.
        if (gestureParam[i] >= 0 && (gestureParam[i] != index || (value < 0 && now - lastMoved[i] > gestureIdleMs)))
        {
            if (auto* open = parameterAt (gestureParam[i]))
                open->endChangeGesture();
            gestureParam[i] = -1;
        }
        auto* p = parameterAt (index);
        if (p == nullptr || value < 0)
            continue;
        if (gestureParam[i] < 0)
        {
            p->beginChangeGesture();
            gestureParam[i] = index;
        }
        p->setValueNotifyingHost (static_cast<float> (value) / 127.0f);
        lastMoved[i] = now;
    }
}

} // namespace osp::plugin
