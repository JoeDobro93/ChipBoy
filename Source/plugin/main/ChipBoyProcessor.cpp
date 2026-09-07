#include "plugin/main/ChipBoyProcessor.h"

#include "plugin/shared/BankJson.h"

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::link;

namespace {
constexpr int kTimerMs = 100;
constexpr size_t kMaxPendingLink = 4096;
}

AudioProcessorValueTreeState::ParameterLayout ChipBoyProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout L;
    addGlobalParameters(L);
    for (int ch = 0; ch < 4; ++ch) addChannelParameters(L, channelPrefix(ch), kindOf(ch), true);
    return L;
}

ChipBoyProcessor::ChipBoyProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ChipBoy", createLayout()),
      uuid_(Uuid().toString())
{
    for (auto& l : channelLevels) l.store(-1);
    for (auto& l : lastNotes) l.store(-1);
    for (int ch = 0; ch < 4; ++ch) channelParams[size_t(ch)].bind(apvts, channelPrefix(ch));
    auto g = [&](const char* id) { return apvts.getRawParameterValue(id); };
    pModel_ = g(ids::model); pMasterL_ = g(ids::masterL); pMasterR_ = g(ids::masterR); pTrim_ = g(ids::trim);
    pNoise_ = g(ids::noise); pLcd_ = g(ids::lcd); pBassMod_ = g(ids::bassMod); pEdges_ = g(ids::volEdges);
    pDeclick_ = g(ids::declick); pDeclickMs_ = g(ids::declickMs); pSoften_ = g(ids::soften);
    pTickSource_ = g(ids::tickSource); pTicksPerBeat_ = g(ids::ticksPerBeat); pTickHz_ = g(ids::tickHz); pLink_ = g(ids::linkMode);

    events_.reserve(2048); delayed_.reserve(2048); writes_.reserve(8192);
    pendingLink_.reserve(kMaxPendingLink); pendingScratch_.reserve(kMaxPendingLink);
    cycleAt_ = [this](uint64_t f) { return renderer_.cycleForFrame(f); };

    publishBank(std::make_shared<const bank::Bank>(bank::Bank::factory()));
    publishSong(std::make_shared<const tracker::Song>());
    startTimer(kTimerMs);
}

ChipBoyProcessor::~ChipBoyProcessor()
{
    stopTimer();
    linkHost_.unpublish();
}

/* -------------------------------------------------------- bank & song */

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

void ChipBoyProcessor::mutateBank(const std::function<void(bank::Bank&)>& fn)
{
    auto copy = std::make_shared<bank::Bank>(bankShared_ ? *bankShared_ : bank::Bank::empty());
    fn(*copy);
    publishBank(std::move(copy));
}

void ChipBoyProcessor::mutateSong(const std::function<void(tracker::Song&)>& fn)
{
    auto copy = std::make_shared<tracker::Song>(songShared_ ? *songShared_ : tracker::Song{});
    fn(*copy);
    publishSong(std::move(copy));
}

/* --------------------------------------------------------- lifecycle */

void ChipBoyProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    blockSize_ = std::max(16, samplesPerBlock);
    modelIndex_ = -1;
    const int model = paramInt(pModel_);
    const Console console = model == 1 ? Console::CGB : Console::DMG;
    renderer_.prepare(sampleRate, AnalogModel::forConsole(console), std::max(blockSize_, 4096));
    driver_.prepare(sampleRate, bankPtr_.load(), songPtr_.load(), console);
    player_.prepare(sampleRate);
    apu_.reset();
    frames_ = 0; prevBlock_ = 0;
    events_.clear(); delayed_.clear(); writes_.clear(); pendingLink_.clear();
    lastHostFrame_ = kNoHostFrame;
    scopes_.master.sampleRate = sampleRate;
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

/* -------------------------------------------------------------- MIDI */

void ChipBoyProcessor::routeMidi(const MidiMessage& m, int offset, std::vector<driver::NoteEvent>& dst)
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
        const SourceChoice src = decodeSource(paramInt(channelParams[size_t(ch)].source));
        if (src.off) continue;
        if (!src.omni && src.midiChannel != midiCh) continue;
        e.channel = uint8_t(ch);
        dst.push_back(e);
    }
}

/* -------------------------------------------------------------- link */

void ChipBoyProcessor::placeLinkEvent(const LinkEvent& e, int n, uint64_t hostFrame, bool hostTimeKnown)
{
    const int ch = e.note.channel & 3;
    uint32_t off = std::min<uint32_t>(e.offset, uint32_t(n - 1));
    const bool frozen = hostFrame == lastHostFrame_;
    if (hostTimeKnown && e.hostFrame != kNoHostFrame && !frozen) {
        // The block being rendered is the previous host block (section 11.4).
        const int64_t start = int64_t(hostFrame) - int64_t(prevBlock_ > 0 ? prevBlock_ : n);
        const int64_t rel = int64_t(e.hostFrame + e.offset) - start;
        if (rel < 0) off = 0;
        else if (rel < n) off = uint32_t(rel);
        else if (rel < int64_t(sampleRate_ * 2.0) && pendingLink_.size() < kMaxPendingLink) { pendingLink_.push_back(e); return; }   // ahead of us: keep
        // else: a timestamp from another timeline; play it now
    }
    if (e.kind == LinkEvent::Params) { linkParams_[size_t(ch)] = e.params; haveLinkParams_[size_t(ch)] = true; return; }
    driver::NoteEvent ne = e.note;
    ne.offset = off; ne.channel = uint8_t(ch); ne.source = driver::NoteEvent::Midi;
    events_.push_back(ne);
}

void ChipBoyProcessor::consumeLink(int n, uint64_t hostFrame, bool hostTimeKnown)
{
    auto* r = linkHost_.region();
    if (!r) { pendingLink_.clear(); for (auto& h : haveLinkParams_) h = false; for (auto& l : localOn_) l = false; return; }
    const uint32_t owned = linkHost_.ownedMask();

    // Events held from earlier blocks come first: they are older.
    pendingScratch_.swap(pendingLink_);
    pendingLink_.clear();
    for (const auto& e : pendingScratch_) if (owned & (1u << (e.note.channel & 3))) placeLinkEvent(e, n, hostFrame, hostTimeKnown);
    pendingScratch_.clear();

    for (int ch = 0; ch < 4; ++ch) {
        auto& s = r->slots[ch];
        if (!(owned & (1u << ch))) { haveLinkParams_[size_t(ch)] = false; localOn_[size_t(ch)] = false; continue; }
        if (s.useLocal.load(std::memory_order_acquire)) {
            const uint32_t v = s.local.version();
            if (!localOn_[size_t(ch)] || v != localSeq_[size_t(ch)]) {
                bank::InstrumentCore core;
                if (s.local.read(core)) {
                    static_cast<bank::InstrumentCore&>(localInst_[size_t(ch)]) = core;
                    localInst_[size_t(ch)].used = true;
                    localSeq_[size_t(ch)] = v; localOn_[size_t(ch)] = true;
                }
            }
        } else localOn_[size_t(ch)] = false;

        LinkEvent e;
        for (uint32_t guard = 0; guard < kEventRing && s.events.pop(e); ++guard) {
            e.note.channel = uint8_t(ch);
            placeLinkEvent(e, n, hostFrame, hostTimeKnown);
        }
    }
}

/* ------------------------------------------------------------ record */

void ChipBoyProcessor::recordNote(const driver::NoteEvent& e, const driver::Transport& t, int n)
{
    int bar = 0, step = 0; double at = 0.0;
    if (!player_.quantise(t, e.offset, uint32_t(n), bar, step, at)) return;
    tracker::RecordMessage m;
    m.channel = uint8_t(e.channel & 3); m.bar = uint16_t(std::clamp(bar, 0, 65535)); m.step = uint8_t(std::clamp(step, 0, tracker::kSteps - 1));
    if (e.kind == driver::NoteEvent::NoteOff) { m.cell.note = tracker::kNoteOff; recordFifo_.push(m); return; }
    const auto& p = driver_.params(e.channel & 3);
    m.cell.note = e.a; m.cell.inst = p.instrument; m.cell.table = p.table;
    int slot = 0;
    auto put = [&](bank::Command c) { if (slot == 0) m.cell.cmd1 = c; else if (slot == 1) m.cell.cmd2 = c; ++slot; };
    if (p.vibDepth != 255 || p.vibSpeed != 255) put({ bank::Cmd::V, int16_t(p.vibSpeed == 255 ? 4 : p.vibSpeed), int16_t(p.vibDepth == 255 ? 0 : p.vibDepth), 0 });
    if (p.detune != 0) put({ bank::Cmd::P, p.detune, 0, 0 });
    if (p.pan != 255) put({ bank::Cmd::O, int16_t(p.pan & 3), 0, 0 });
    if (p.envVol != 255) put({ bank::Cmd::A, int16_t(p.envVol), int16_t(p.envRate == 255 ? 0 : p.envRate), int16_t(p.envDir == 255 ? 0 : p.envDir) });
    recordFifo_.push(m);
}

void ChipBoyProcessor::applyRecordMessages()
{
    tracker::RecordMessage m;
    std::shared_ptr<tracker::Song> copy;
    while (recordFifo_.pop(m)) {
        if (!copy) copy = std::make_shared<tracker::Song>(songShared_ ? *songShared_ : tracker::Song{});
        auto& chain = copy->chain[size_t(m.channel & 3)];
        if (chain.size() <= m.bar) chain.resize(size_t(m.bar) + 1, 0);
        uint8_t slot = chain[m.bar];
        if (slot == 0) {
            for (int i = 0; i < tracker::kPhraseSlots; ++i) if (!copy->phrases[size_t(i)].used) { slot = uint8_t(i + 1); break; }
            if (slot == 0) continue;                    // the song is full
            copy->phrases[size_t(slot - 1)].used = true;
            chain[m.bar] = slot;
        }
        auto& cell = copy->phrases[size_t(slot - 1)].steps[size_t(m.step & 15)];
        if (m.cell.note == tracker::kNoteOff) { if (cell.note == 0) cell.note = tracker::kNoteOff; }
        else cell = m.cell;
    }
    if (copy) publishSong(std::move(copy));
}

/* --------------------------------------------------------- the timer */

void ChipBoyProcessor::publishInstrumentNames()
{
    if (!bankShared_) return;
    std::vector<std::pair<String, int>> names;
    names.reserve(128);
    for (const auto& i : bankShared_->instruments) names.emplace_back(i.used ? String(i.name) : String(), i.used ? int(i.type) : -1);
    linkHost_.setInstrumentNames(names);
    namesPublishedFor_ = bankShared_.get();
}

void ChipBoyProcessor::handleVoiceRequests()
{
    auto* r = linkHost_.region();
    if (!r) return;
    for (int ch = 0; ch < 4; ++ch) {
        auto& s = r->slots[ch];
        if (s.focusRequest.exchange(0)) focusRequest_.store(ch);
        const uint32_t serial = s.requestSerial.load(std::memory_order_acquire);
        if (serial == s.ackSerial.load()) continue;
        const auto req = Request(s.request.load());
        const int slot = int(s.requestSlot.load());
        uint32_t result = 1;
        if (slot >= 1 && slot <= bank::kInstrumentSlots) {
            if (req == Request::PushToSlot) {
                bank::InstrumentCore core;
                if (s.exchange.read(core)) {
                    char n[kInstNameChars]; safeString(s.exchangeName, kInstNameChars, n, kInstNameChars);
                    mutateBank([&](bank::Bank& b) {
                        auto& i = b.instruments[size_t(slot - 1)];
                        static_cast<bank::InstrumentCore&>(i) = core;
                        i.used = true; i.name = n;
                    });
                    result = 0;
                }
            } else if (req == Request::PullFromSlot && bankShared_) {
                const auto& i = bankShared_->instruments[size_t(slot - 1)];
                if (i.used) {
                    s.exchange.write(i);
                    setString(s.exchangeName, kInstNameChars, i.name.c_str());
                    result = 0;
                }
            }
        }
        s.ackResult.store(result);
        s.ackSerial.store(serial, std::memory_order_release);
    }
}

void ChipBoyProcessor::timerCallback()
{
    const bool want = paramInt(pLink_) != 0;
    if (want != linkActive_) {
        linkActive_ = want;
        if (want) {
            String u = uuid_;
            if (linkHost_.publish(u, instanceName_)) uuid_ = u;
            namesPublishedFor_ = nullptr;
        } else linkHost_.unpublish();
        setLatencySamples(renderer_.latencyFrames() + (want ? blockSize_ : 0));
    }
    if (linkActive_) {
        if (!linkHost_.published()) { String u = uuid_; if (linkHost_.publish(u, instanceName_)) uuid_ = u; namesPublishedFor_ = nullptr; }
        linkHost_.maintain(instanceName_, sampleRate_, blockSize_, true, paramInt(pModel_));
        if (namesPublishedFor_ != bankShared_.get()) publishInstrumentNames();
        handleVoiceRequests();
    }
    applyRecordMessages();
}

/* ------------------------------------------------------------- audio */

void ChipBoyProcessor::tapScopes(int n, const float* L, const float* R)
{
    auto* r = linkActive_ ? linkHost_.region() : nullptr;
    const uint32_t owned = r ? linkHost_.ownedMask() : 0;
    for (const auto& e : apu_.events()) {
        const int lv = e.dacOn ? int(e.level) : -1;
        scopes_.channels[e.channel & 3].push(e.cycle, lv);
        if (owned & (1u << (e.channel & 3))) r->slots[e.channel & 3].scope.push(e.cycle, lv);
    }
    (void)L; (void)R;
    scopes_.latestCycle.store(apu_.cycle(), std::memory_order_release);
    scopes_.latestFrame.store(frames_ + uint64_t(n), std::memory_order_release);
    for (int ch = 0; ch < 4; ++ch) {
        const uint64_t st = packState(driver_.view(ch));
        scopes_.state[size_t(ch)].store(st, std::memory_order_release);
        if (owned & (1u << ch)) r->slots[ch].state.store(st, std::memory_order_release);
    }
    scopes_.mix.store(uint32_t(driver_.nr50()) | (uint32_t(driver_.nr51()) << 8) | (1u << 16), std::memory_order_release);
}

void ChipBoyProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0) return;

    const auto* song = songPtr_.load(std::memory_order_acquire);
    driver_.setBank(bankPtr_.load(std::memory_order_acquire));
    driver_.setSong(song);
    player_.setSong(song);
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
    {
        const uint32_t solo = soloMask_.load(), mute = muteMask_.load();
        driver_.setGateMask((solo ? solo : 15u) & ~mute & 15u);
    }

    // --- transport ------------------------------------------------------
    driver::Transport t;
    uint64_t hostFrame = kNoHostFrame;
    double beatsPerBar = 4.0;
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            t.playing = pos->getIsPlaying();
            const auto bpm = pos->getBpm(); const auto ppq = pos->getPpqPosition();
            t.valid = bpm.hasValue() && ppq.hasValue();
            t.bpm = bpm.orFallback(120.0); t.ppq = ppq.orFallback(0.0);
            if (const auto ts = pos->getTimeSignature()) if (ts->numerator > 0 && ts->denominator > 0) beatsPerBar = ts->numerator * 4.0 / ts->denominator;
            if (const auto tis = pos->getTimeInSamples()) if (*tis >= 0) hostFrame = uint64_t(*tis);
        }
    }
    const bool link = linkActive_;
    const double ppqPerFrame = t.bpm / 60.0 / sampleRate_;
    if (link && t.valid) t.ppq -= (prevBlock_ > 0 ? prevBlock_ : n) * ppqPerFrame;   // one block behind (11.4)
    player_.setBeatsPerBar(beatsPerBar);
    player_.setTicksPerBeat(g.ticksPerBeat);
    playing_.store(t.playing); ppq_.store(t.ppq); bpm_.store(t.bpm); beatsPerBar_.store(beatsPerBar);

    // --- events ---------------------------------------------------------
    events_.clear();
    if (link) {
        // This block's MIDI waits a block so it lines up with the Voices.
        events_.swap(delayed_);
        delayed_.clear();
        for (auto& e : events_) e.offset = std::min<uint32_t>(e.offset, uint32_t(n - 1));
        for (const auto meta : midi) routeMidi(meta.getMessage(), meta.samplePosition, delayed_);
    } else {
        for (const auto meta : midi) routeMidi(meta.getMessage(), meta.samplePosition, events_);
    }

    uint32_t owned = 0;
    if (link) {
        owned = linkHost_.ownedMask();
        if (owned) {
            // A channel a Voice owns ignores the main plugin's direct MIDI (11.1).
            events_.erase(std::remove_if(events_.begin(), events_.end(), [owned](const driver::NoteEvent& e) { return (owned & (1u << (e.channel & 3))) != 0; }), events_.end());
        }
        consumeLink(n, hostFrame, hostFrame != kNoHostFrame);
    } else {
        pendingLink_.clear();
    }

    for (int ch = 0; ch < 4; ++ch) {
        const bool viaVoice = (owned & (1u << ch)) && haveLinkParams_[size_t(ch)];
        if (viaVoice) {
            driver::ChannelParams p = linkParams_[size_t(ch)];
            if (ch == 2 && p.level != 255) p.level = uint8_t(p.level == 0 ? 0 : p.level <= 5 ? 1 : p.level <= 10 ? 2 : 3);   // a Voice's 0-15 onto the wave channel's four steps
            driver_.setParams(ch, p);
        } else {
            driver_.setParams(ch, channelParams[size_t(ch)].read(kindOf(ch)));
        }
        driver_.setLocalInstrument(ch, (owned & (1u << ch)) && localOn_[size_t(ch)] ? &localInst_[size_t(ch)] : nullptr);
    }

    // --- the tracker ----------------------------------------------------
    uint32_t trackerMask = 0;
    if (song) for (int ch = 0; ch < 4; ++ch) if (song->noteSource[size_t(ch)] == tracker::NoteSource::Tracker) trackerMask |= 1u << ch;
    const bool rec = recordArm_.load() && t.playing && t.valid;
    player_.setMuteMask(rec ? trackerMask : 0);
    driver_.setRecording(rec);
    player_.process(t, uint32_t(n), events_);
    if (rec)
        for (const auto& e : events_)
            if (e.source == driver::NoteEvent::Midi && (trackerMask & (1u << (e.channel & 3))) && (e.kind == driver::NoteEvent::NoteOn || e.kind == driver::NoteEvent::NoteOff))
                recordNote(e, t, n);

    std::stable_sort(events_.begin(), events_.end(), [](const driver::NoteEvent& a, const driver::NoteEvent& b) { return a.offset < b.offset; });
    for (const auto& e : events_) {
        if (e.kind == driver::NoteEvent::NoteOn && e.b) lastNotes[size_t(e.channel & 3)].store(e.a);
        else if (e.kind == driver::NoteEvent::NoteOff || (e.kind == driver::NoteEvent::NoteOn && !e.b)) { const int cur = lastNotes[size_t(e.channel & 3)].load(); if (cur == e.a) lastNotes[size_t(e.channel & 3)].store(-1); }
    }

    // --- drive the chip -------------------------------------------------
    writes_.clear();
    driver_.process(events_.data(), events_.size(), uint32_t(n), frames_, t, cycleAt_, writes_);
    applyWrites();
    apu_.runTo(std::max(renderer_.cycleForFrame(frames_ + uint64_t(n)), apu_.cycle()));
    for (int ch = 0; ch < 4; ++ch) channelLevels[size_t(ch)].store(apu_.dacOn(ch) ? apu_.level(ch) : -1);

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    tapScopes(n, L, R);   // reads the event list before render() clears it
    if (R) renderer_.render(apu_, L, R, n);
    else renderer_.render(apu_, L, L, n);
    scopes_.master.push(L, R, uint32_t(n));
    const float gain = 0.25f * Decibels::decibelsToGain(pTrim_ ? pTrim_->load() : -6.0f);
    buffer.applyGain(gain);

    frames_ += uint64_t(n);
    prevBlock_ = n;
    lastHostFrame_ = hostFrame;
}

void ChipBoyProcessor::applyWrites()
{
    // Writes arrive in cycle order. A quiet-edge marker asks for the rest of
    // its channel's burst to wait for the pulse output's low half: those
    // writes move later, which can put them after other channels' writes,
    // so the moved ones are re-sorted before they are applied.
    for (size_t i = 0; i < writes_.size(); ++i) {
        const auto& w = writes_[i];
        if (w.addr != driver::Driver::kAlignToQuietEdge) continue;
        apu_.runTo(std::max(w.cycle, apu_.cycle()));
        const uint64_t delay = apu_.cyclesUntilPulseLow(w.value & 1);
        if (delay == 0) continue;
        const int ch = w.value & 1;
        const uint16_t lo = uint16_t(0xFF10 + ch * 5), hi = uint16_t(lo + 4);
        for (size_t j = i + 1; j < writes_.size(); ++j) {
            if (writes_[j].addr == driver::Driver::kAlignToQuietEdge) break;
            if (writes_[j].addr >= lo && writes_[j].addr <= hi) writes_[j].cycle += delay;
        }
        // Only the tail from here can be out of order now.
        std::stable_sort(writes_.begin() + long(i + 1), writes_.end(), [](const driver::RegWrite& a, const driver::RegWrite& b) { return a.cycle < b.cycle; });
    }
    for (const auto& w : writes_) {
        if (w.addr == driver::Driver::kAlignToQuietEdge) continue;
        apu_.runTo(std::max(w.cycle, apu_.cycle()));
        apu_.write(w.addr, w.value);
    }
}

/* ------------------------------------------------------------- state */

AudioProcessorEditor* ChipBoyProcessor::createEditor()
{
    return new GenericAudioProcessorEditor(*this);
}

void ChipBoyProcessor::getStateInformation(MemoryBlock& dest)
{
    ValueTree root("ChipBoyState");
    root.setProperty("version", 1, nullptr);
    root.setProperty("uuid", uuid_, nullptr);
    root.setProperty("name", instanceName_, nullptr);
    root.setProperty("bankName", bankName_, nullptr);
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
    if (root.hasProperty("uuid") && root["uuid"].toString().isNotEmpty()) {
        const String u = root["uuid"].toString();
        if (u != uuid_) { uuid_ = u; if (linkHost_.published()) linkHost_.unpublish(); }   // the timer republishes under the saved UUID
    }
    if (root.hasProperty("name")) instanceName_ = root["name"].toString();
    if (root.hasProperty("bankName")) bankName_ = root["bankName"].toString();
    const ValueTree params = root.getChildWithName(apvts.state.getType());
    if (params.isValid()) apvts.replaceState(params);
    if (root.hasProperty("bank")) { auto b = std::make_shared<bank::Bank>(); if (bankFromJson(root["bank"].toString(), *b)) publishBank(b); }
    if (root.hasProperty("song")) { auto s = std::make_shared<tracker::Song>(); if (songFromJson(root["song"].toString(), *s)) publishSong(s); }
}

} // namespace chipboy::plugin

#ifndef CHIPBOY_NO_PLUGIN_FILTER
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chipboy::plugin::ChipBoyProcessor();
}
#endif
