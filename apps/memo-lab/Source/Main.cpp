// ANDOR Memo Lab: the application. One ANDOR OSP processor on the Mac's audio device (MIDI
// inputs go to it, as to the plugin in a host), the preview mixed on the same device, and the
// window (MemoLabWindow). docs/research/memo-lab.

#include "MemoLabWindow.h"
#include "OspLookAndFeel.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace osp::memolab
{

class MemoLabApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "ANDOR Memo Lab"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
        processor = std::make_unique<OspAudioProcessor>();

        // The device as last chosen (or the default output), every MIDI input enabled (and
        // any connected later), and the processor played on it.
        const auto saved = juce::XmlDocument::parse (deviceFile());
        devices.initialise (0, 2, saved.get(), true);
        enableMidiInputs();
        midiDevicesChanged = juce::MidiDeviceListConnection::make ([this] { enableMidiInputs(); });
        devices.addMidiInputDeviceCallback ({}, &player);
        player.setProcessor (processor.get());
        devices.addAudioCallback (&player);

        settings = std::make_unique<SourceSettings>();
        window = std::make_unique<Window> (*this);
        devices.addAudioCallback (&window->content->preview().callback());
    }

    void shutdown() override
    {
        if (window != nullptr)
            devices.removeAudioCallback (&window->content->preview().callback());
        devices.removeAudioCallback (&player);
        devices.removeMidiInputDeviceCallback ({}, &player);
        if (auto state = devices.createStateXml())
            state->writeTo (deviceFile());
        window.reset();
        player.setProcessor (nullptr);
        processor.reset();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    static juce::File deviceFile() { return appDataFolder().getChildFile ("audio-device.xml"); }

    void enableMidiInputs()
    {
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            if (! devices.isMidiInputDeviceEnabled (input.identifier))
                devices.setMidiInputDeviceEnabled (input.identifier, true);
    }

    struct Window final : juce::DocumentWindow
    {
        explicit Window (MemoLabApplication& app)
            : juce::DocumentWindow ("ANDOR Memo Lab", osp::plugin::design::colour::housingTop, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            content = new MemoLabComponent (*app.processor, &app.devices, *app.settings);
            setContentOwned (content, false);
            setResizable (true, true);
            setResizeLimits (980, 680, 2400, 1600);
            centreWithSize (1240, 820);
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        MemoLabComponent* content = nullptr;
    };

    osp::plugin::OspLookAndFeel lookAndFeel;
    std::unique_ptr<OspAudioProcessor> processor;
    juce::AudioDeviceManager devices;
    juce::AudioProcessorPlayer player;
    juce::MidiDeviceListConnection midiDevicesChanged;
    std::unique_ptr<SourceSettings> settings;
    std::unique_ptr<Window> window;
};

} // namespace osp::memolab

// The plugin's shared code is built for a plugin, where JUCE's START_JUCE_APPLICATION is
// reserved for its own standalone wrapper; this is the same definition, written out.
juce::JUCEApplicationBase* juce_CreateApplication();
juce::JUCEApplicationBase* juce_CreateApplication() { return new osp::memolab::MemoLabApplication(); }

int main (int argc, char* argv[])
{
    juce::JUCEApplicationBase::createInstance = &juce_CreateApplication;
    return juce::JUCEApplicationBase::main (argc, const_cast<const char**> (argv));
}
