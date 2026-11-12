#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Presets.h"
#include "Rack/Rack.h"

namespace comma
{
// Эффект: аудио стерео → стерео, MIDI/MPE на входе (ноты понадобятся модулям с этапа 3).
// Состав rack'а хранится в состоянии: узел Rack → Slot (module, parallel); правки идут через UndoManager.
class CommaProcessor : public juce::AudioProcessor, private juce::ValueTree::Listener, private juce::Timer
{
public:
    CommaProcessor();
    ~CommaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Comma"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- rack (поток сообщений)
    RackLayout getLayout() const { return layout; }
    void setLayout (const RackLayout& l);   // пишет в состояние с undo
    bool addModule (int m);
    void removeSlot (int slot);
    void moveSlot (int from, int to);
    void setParallel (int slot, bool parallel);
    static RackLayout layoutFromTree (const juce::ValueTree& state);
    static void writeLayoutToTree (juce::ValueTree& state, const RackLayout& l, juce::UndoManager* um);

    juce::UndoManager undo { 30000, 60 };
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;
    Rack rack;

    // ---- для интерфейса
    std::atomic<float> outPeak { 0.0f }, inPeak { 0.0f }, cpuLoad { 0.0f };
    std::atomic<int> midiNotes { 0 };   // сколько нот держится (MIDI принимается уже на этапе 0)
    int getReportedLatency() const { return getLatencySamples(); }

private:
    void syncLayoutFromState();
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { syncLayoutFromState(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { syncLayoutFromState(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { syncLayoutFromState(); }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) override { if (t.hasType ("Slot")) syncLayoutFromState(); }
    void valueTreeRedirected (juce::ValueTree&) override { syncLayoutFromState(); }
    void timerCallback() override;

    RackLayout layout;
    std::array<std::atomic<float>*, kNumModules> mixParam {}, onParam {};
    std::atomic<float>* outputParam = nullptr;
    juce::SmoothedValue<float> outGain;
    std::array<juce::uint8, 16 * 128> heldCount {};   // нажатые ноты по каналу и номеру
    double sr = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommaProcessor)
};
} // namespace comma
