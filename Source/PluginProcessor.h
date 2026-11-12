#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Presets.h"
#include "Rack/Rack.h"
#include "Tuning/MTS.h"
#include "DSP/DSP.h"

namespace comma
{
// Эффект: аудио стерео → стерео, MIDI/MPE на входе.
// Состав rack'а хранится в состоянии: узел Rack → Slot (module, parallel); правки идут через UndoManager.
// Строй: параметры tun_* и узел Tuning (ratios, scl, kbm, sclName, kbmName, mtsMaster).
class CommaProcessor : public juce::AudioProcessor, private juce::ValueTree::Listener, private juce::Timer
{
public:
    CommaProcessor();
    ~CommaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;   // double-версия — по умолчанию JUCE

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Comma"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

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

    // ---- строй (поток сообщений)
    void refreshTuning (bool force = false);   // пересобрать снимок, если что-то сменилось
    TuningPtr getTuning() const { return tuningHolder.latest(); }
    juce::String getTuningError() const { return tuningError; }
    bool loadScalaFile (const juce::File& f, juce::String& error);   // .scl или .kbm
    bool setRatios (const juce::String& text, juce::String& error);
    juce::String getRatios() const;
    void clearKbm();
    bool setMtsMaster (bool on);
    bool isMtsMaster() const { return mts.isMaster(); }
    bool hasMtsMaster() const { return mts.hasMaster(); }
    void updateLatencies();

    // ---- значения для интерфейса и тестов
    float getParam (int p) const { return raw[(size_t) p]->load(); }
    void setParam (int p, float value);   // с жестом, для интерфейса и кубика

    juce::UndoManager undo { 30000, 60 };
    juce::AudioProcessorValueTreeState apvts;
    PresetManager presets;
    Rack rack;
    NoteLayer notes;   // аудиопоток

    // ---- для интерфейса
    std::atomic<float> outPeak { 0.0f }, inPeak { 0.0f }, cpuLoad { 0.0f };
    std::atomic<int> midiNotes { 0 }, mpeMembers { 0 };
    struct VoiceView { std::atomic<int> active { 0 }, held { 0 }, note { 0 }; std::atomic<float> pitch { 0 }, pressure { 0 }, slide { 0 }, bend { 0 }, strike { 0 }; };
    std::array<VoiceView, kMaxVoices> voiceView;
    int getReportedLatency() const { return getLatencySamples(); }
    // высота входа (поток сообщений): забирает накопленное и считает YIN
    void analyseInput();
    float getInputHz() const { return inputHz; }
    static constexpr int kHistory = 160;
    std::array<float, kHistory> centsHistory {};   // отклонение от ближайшей ступени, NaN — нет высоты
    int historyPos = 0;

private:
    void syncLayoutFromState();
    juce::ValueTree tuningTree();
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override { syncLayoutFromState(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { syncLayoutFromState(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override { syncLayoutFromState(); }
    void valueTreePropertyChanged (juce::ValueTree& t, const juce::Identifier&) override
    {
        if (t.hasType ("Slot")) syncLayoutFromState();
        if (t.hasType ("Tuning")) tuningDirty = true;
    }
    void valueTreeRedirected (juce::ValueTree&) override { syncLayoutFromState(); tuningDirty = true; }
    void timerCallback() override;

    RackLayout layout;
    std::array<std::atomic<float>*, P::Count> raw {};
    std::array<float, P::Count> values {}, offsets {};
    ModMatrix matrix;
    juce::SmoothedValue<float> outGain;
    std::array<juce::uint8, 16 * 128> heldCount {};   // нажатые ноты по каналу и номеру
    double sr = 48000.0;
    int maxBlock = 512;

    TuningHolder tuningHolder;
    TuningSpec builtSpec;
    bool tuningDirty = true, builtOnce = false;
    juce::String tuningError, builtMtsName;
    MTSLink mts;

    juce::AbstractFifo inFifo { 16384 };
    std::vector<float> inFifoData = std::vector<float> (16384, 0.0f);
    dsp::YinTracker uiYin;
    float inputHz = 0.0f;
    bool uiYinReady = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CommaProcessor)
};
} // namespace comma
