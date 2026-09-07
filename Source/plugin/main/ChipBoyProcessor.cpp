#include "plugin/main/ChipBoyProcessor.h"

#include "plugin/shared/BankJson.h"

namespace chipboy::plugin {

using namespace juce;

AudioProcessorValueTreeState::ParameterLayout ChipBoyProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout L;
    addGlobalParameters(L);
    for (int ch = 0; ch < 4; ++ch) addChannelParameters(L, channelPrefix(ch), kindOf(ch), true);
    return L;
}

ChipBoyProcessor::ChipBoyProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ChipBoy", createLayout())
{
    for (auto& l : channelLevels) l.store(0);
    for (int ch = 0; ch < 4; ++ch) chParams_[size_t(ch)].bind(apvts, channelPrefix(ch));
    auto g = [&](const char* id) { return apvts.getRawParameterValue(id); };
    pModel_ = g(ids::model); pMasterL_ = g(ids::masterL); pMasterR_ = g(ids::masterR); pTrim_ = g(ids::trim);
    pNoise_ = g(ids::noise); pLcd_ = g(ids::lcd); pBassMod_ = g(ids::bassMod); pEdges_ = g(ids::volEdges);
    pDeclick_ = g(ids::declick); pDeclickMs_ = g(ids::declickMs); pSoften_ = g(ids::soften);
    pTickSource_ = g(ids::tickSource); pTicksPerBeat_ = g(ids::ticksPerBeat); pTickHz_ = g(ids::tickHz); pLink_ = g(ids::linkMode);

    events_.reserve(1024); delayed_.reserve(1024); writes_.reserve(8192);
    cycleAt_ = [this](uint64_t f) { return renderer_.cycleForFrame(f); };

    publishBank(std::make_shared<const bank::Bank>(bank::Bank::factory()));
    publishSong(std::make_shared<const tracker::Song>());
}

ChipBoyProcessor::~ChipBoyProcessor() = default;

void ChipBoyProcessor::publishBank(std::shared_ptr<const bank::Bank> b)
{
    if (!b) return;
    if (bankShared_) retired_.push_back(bankShared_);
    while (retired_.size() > 32) retired_.pop_front();
    bankShared_ = std::move(b);
    bankPtr_.store(bankShared_.get(), std::memory_order_release);
}

void ChipBoyProcessor::publishSong(std::shared_ptr<const tracker::Song> s)
{
    if (!s) return;
    if (songShared_) retired_.push_back(songShared_);
    while (retired_.size() > 32) retired_.pop_front();
    songShared_ = std::move(s);
    songPtr_.store(songShared_.get(), std::memory_order_release);
}

void ChipBoyProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    blockSize_ = std::max(16, samplesPerBlock);
    modelIndex_ = -1;
    const int model = paramInt(pModel_);
    renderer_.prepare(sampleRate, AnalogModel::forConsole(model == 1 ? Console::CGB : Console::DMG), std::max(blockSize_, 4096));
    driver_.prepare(sampleRate, bankPtr_.load(), songPtr_.load(), model == 1 ? Console::CGB : Console::DMG);
    apu_.reset();
    frames_ = 0;
    events_.clear(); delayed_.clear(); writes_.clear();
    applyModel();
    linkActive_ = paramInt(pLink_) != 0;
    setLatencySamples(renderer_.latencyFrames() + (linkActive_ ? blockSize_ : 0));
}

bool ChipBoyProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono();
}

void ChipBoyProcessor::applyModel()
{
    const int model = paramInt(pModel_);
    if (model == modelIndex_) return;
    modelIndex_ = model;
    const Console console = model == 1 ? Console::CGB : Console::DMG;
    renderer_.setModel(AnalogModel::forConsole(console), /*bypassAnalog*/ model == 2);
    driver_.setModel(console);
    apu_.setModel(console);
}

void ChipBoyProcessor::applyHardwareOptions()
{
    render::Renderer::Options o;
    o.noise = paramInt(pNoise_) != 0;
    o.lcd = paramInt(pLcd_) != 0;
    o.bassMod = uint8_t(std::clamp(paramInt(pBassMod_), 0, 2));
    o.declickMs = paramInt(pDeclick_) ? pDeclickMs_->load() : 0.0f;
    o.softenMaster = paramInt(pSoften_) != 0;
    renderer_.setOptions(o);
}

void ChipBoyProcessor::routeMidi(const MidiMessage& m, int offset)
{
    driver::NoteEvent e;
    e.offset = uint32_t(std::max(0, offset));
    const int midiCh = m.getChannel();   // 1-16, 0 for non-channel messages
    if (m.isNoteOn()) { e.kind = driver::NoteEvent::NoteOn; e.a = uint8_t(m.getNoteNumber()); e.b = uint8_t(m.getVelocity()); }
    else if (m.isNoteOff()) { e.kind = driver::NoteEvent::NoteOff; e.a = uint8_t(m.getNoteNumber()); }
    else if (m.isPitchWheel()) { e.kind = driver::NoteEvent::PitchBend; e.value = int16_t(m.getPitchWheelValue() - 8192); }
    else if (m.isController()) { e.kind = driver::NoteEvent::Control; e.a = uint8_t(m.getControllerNumber()); e.b = uint8_t(m.getControllerValue()); }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) { e.kind = driver::NoteEvent::AllNotesOff; }
    else return;
    for (int ch = 0; ch < 4; ++ch) {
        const SourceChoice src = decodeSource(paramInt(chParams_[size_t(ch)].source));
        if (src.off) continue;
        if (!src.omni && src.midiChannel != midiCh) continue;
        e.channel = uint8_t(ch);
        events_.push_back(e);
    }
}

void ChipBoyProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0) return;

    driver_.setBank(bankPtr_.load(std::memory_order_acquire));
    driver_.setSong(songPtr_.load(std::memory_order_acquire));
    applyModel();
    applyHardwareOptions();

    driver::GlobalParams g;
    g.masterL = uint8_t(std::clamp(paramInt(pMasterL_, 7), 0, 7));
    g.masterR = uint8_t(std::clamp(paramInt(pMasterR_, 7), 0, 7));
    g.tick = driver::TickSource(std::clamp(paramInt(pTickSource_), 0, 2));
    g.ticksPerBeat = uint8_t(std::clamp(paramInt(pTicksPerBeat_, 24), 1, 48));
    g.customHz = pTickHz_ ? double(pTickHz_->load()) : 60.0;
    g.volumeAtEdges = paramInt(pEdges_) != 0;
    driver_.setGlobal(g);
    for (int ch = 0; ch < 4; ++ch) driver_.setParams(ch, chParams_[size_t(ch)].read(kindOf(ch)));

    driver::Transport t;
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            t.playing = pos->getIsPlaying();
            const auto bpm = pos->getBpm(); const auto ppq = pos->getPpqPosition();
            t.valid = bpm.hasValue() && ppq.hasValue();
            t.bpm = bpm.orFallback(120.0); t.ppq = ppq.orFallback(0.0);
        }
    }

    // MIDI, routed by each channel's source. With link mode on, everything
    // is delayed by one block so Voice plugins line up (section 11.4).
    events_.clear();
    if (linkActive_) {
        events_.swap(delayed_);
        for (auto& e : events_) e.offset = std::min<uint32_t>(e.offset, uint32_t(n - 1));
        delayed_.clear();
        for (const auto meta : midi) routeMidi(meta.getMessage(), meta.samplePosition);
        events_.swap(delayed_);       // this block's go to the next; last block's are in events_
        std::swap(events_, delayed_); // (swap back: events_ = previous block's)
    } else {
        for (const auto meta : midi) routeMidi(meta.getMessage(), meta.samplePosition);
    }
    std::stable_sort(events_.begin(), events_.end(), [](const driver::NoteEvent& a, const driver::NoteEvent& b) { return a.offset < b.offset; });

    writes_.clear();
    driver_.process(events_.data(), events_.size(), uint32_t(n), frames_, t, cycleAt_, writes_);
    for (const auto& w : writes_) {
        apu_.runTo(std::max(w.cycle, apu_.cycle()));
        apu_.write(w.addr, w.value);
    }
    apu_.runTo(std::max(renderer_.cycleForFrame(frames_ + uint64_t(n)), apu_.cycle()));
    for (int ch = 0; ch < 4; ++ch) channelLevels[size_t(ch)].store(apu_.dacOn(ch) ? apu_.level(ch) : -1);

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    if (R) renderer_.render(apu_, L, R, n);
    else { renderer_.render(apu_, L, L, n); }
    const float gain = 0.25f * Decibels::decibelsToGain(pTrim_ ? pTrim_->load() : -6.0f);
    buffer.applyGain(gain);
    frames_ += uint64_t(n);
}

AudioProcessorEditor* ChipBoyProcessor::createEditor()
{
    return new GenericAudioProcessorEditor(*this);
}

void ChipBoyProcessor::getStateInformation(MemoryBlock& dest)
{
    ValueTree root("ChipBoyState");
    root.setProperty("version", 1, nullptr);
    root.setProperty("uuid", uuid_.toString(), nullptr);
    root.setProperty("name", instanceName_, nullptr);
    root.addChild(apvts.copyState(), -1, nullptr);
    if (bankShared_) root.setProperty("bank", bankToJson(*bankShared_), nullptr);
    if (songShared_) root.setProperty("song", songToJson(*songShared_), nullptr);
    MemoryOutputStream mo(dest, false);
    root.writeToStream(mo);
}

void ChipBoyProcessor::setStateInformation(const void* data, int size)
{
    const ValueTree root = ValueTree::readFromData(data, size_t(size));
    if (!root.isValid() || !root.hasType("ChipBoyState")) return;
    if (root.hasProperty("uuid")) uuid_ = Uuid(root["uuid"].toString());
    if (root.hasProperty("name")) instanceName_ = root["name"].toString();
    const ValueTree params = root.getChildWithName(apvts.state.getType());
    if (params.isValid()) apvts.replaceState(params);
    if (root.hasProperty("bank")) { auto b = std::make_shared<bank::Bank>(); if (bankFromJson(root["bank"].toString(), *b)) publishBank(b); }
    if (root.hasProperty("song")) { auto s = std::make_shared<tracker::Song>(); if (songFromJson(root["song"].toString(), *s)) publishSong(s); }
}

} // namespace chipboy::plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chipboy::plugin::ChipBoyProcessor();
}
