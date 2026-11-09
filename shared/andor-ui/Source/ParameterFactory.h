#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

/**
    How the ANDOR parameter schema's parameters are made (InstrumentHost::createParameterLayout,
    the shell's and a product's own): std::make_unique, plus one thing a product may decide,
    which parameters a host may automate.

    A product whose engine reads only part of the schema sets an AutomatableFilter while its
    layout is built (the shell does, from Engine::boundParameters), so the DAW's automation
    lists leave out the parameters that do nothing in it. Nothing else changes: IDs, names,
    ranges, defaults, order and saved state are exactly as before. With no filter (ANDOR OSP's
    own plugin) every parameter is automatable, as always. The filter is per thread: hosts may
    build instances on several threads at once.
*/
namespace osp::plugin::parameters
{
using AutomatableFilter = std::function<bool (const juce::String& parameterId)>;

/** The filter for layouts built on this thread while it lives (the previous one after). */
class ScopedAutomatableFilter
{
public:
    explicit ScopedAutomatableFilter (AutomatableFilter filter);
    ~ScopedAutomatableFilter();
    ScopedAutomatableFilter (const ScopedAutomatableFilter&) = delete;
    ScopedAutomatableFilter& operator= (const ScopedAutomatableFilter&) = delete;

private:
    AutomatableFilter previous;
};

/** Whether a parameter made now on this thread is automatable (true with no filter). */
bool automatableNow (const juce::String& parameterId);

namespace detail
{
    template <typename Param> struct AttributesOf;
    template <> struct AttributesOf<juce::AudioParameterFloat>  { using type = juce::AudioParameterFloatAttributes; };
    template <> struct AttributesOf<juce::AudioParameterChoice> { using type = juce::AudioParameterChoiceAttributes; };
    template <> struct AttributesOf<juce::AudioParameterInt>    { using type = juce::AudioParameterIntAttributes; };
    template <> struct AttributesOf<juce::AudioParameterBool>   { using type = juce::AudioParameterBoolAttributes; };

    template <typename... T> struct Last { using type = typename decltype ((std::type_identity<T> {}, ...))::type; };

    template <typename Param, typename Tuple, std::size_t... I>
    std::unique_ptr<Param> withLastReplaced (const juce::ParameterID& id, Tuple& all, bool automatable, std::index_sequence<I...>)
    {
        return std::make_unique<Param> (id, std::get<I> (all)..., std::get<sizeof...(I)> (all).withAutomatable (automatable));
    }
}

/** std::make_unique<Param> (id, args...), its automatable flag from the filter (attributes
    passed as the last argument are kept otherwise). */
template <typename Param, typename... Args>
std::unique_ptr<Param> make (const juce::ParameterID& id, Args&&... args)
{
    using Attributes = typename detail::AttributesOf<Param>::type;
    const bool automatable = automatableNow (id.getParamID());
    if constexpr (sizeof...(Args) > 0 && std::is_same_v<std::decay_t<typename detail::Last<Args...>::type>, Attributes>)
    {
        auto all = std::forward_as_tuple (std::forward<Args> (args)...);
        return detail::withLastReplaced<Param> (id, all, automatable, std::make_index_sequence<sizeof...(Args) - 1> {});
    }
    else
    {
        static_assert (std::is_constructible_v<Param, const juce::ParameterID&, Args..., Attributes>,
                       "parameters::make needs a constructor that takes attributes");
        return std::make_unique<Param> (id, std::forward<Args> (args)..., Attributes {}.withAutomatable (automatable));
    }
}
} // namespace osp::plugin::parameters
