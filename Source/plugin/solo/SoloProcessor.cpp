#include "plugin/solo/SoloProcessor.h"

#include "plugin/shared/BankJson.h"
#include "plugin/solo/SoloEditor.h"
#include "plugin/ui/Theme.h"

#include <algorithm>
#include <cmath>

namespace chipboy::plugin {

using namespace juce;
namespace sid = chipboy::plugin::solo::ids;

namespace {
constexpr int kTimerMs = 100;
/// Blocks a recall overrides the parameters for at most: the timer writes
/// them within a few and says so, so this only guards a window that never
/// pumps its message thread.
constexpr int kOverrideBlocks = 200;

template <typename T>
std::unique_ptr<AudioParameterInt> intParam(const char* id, const String& name, int lo, int hi, int def, T text)
{
    return std::make_unique<AudioParameterInt>(ParameterID(id, 1), name, lo, hi, def, AudioParameterIntAttributes().withStringFromValueFunction(text));
}

/// A bank or a SoloState before and after a hand edit, restored on undo.
template <typename T>
struct SnapshotAction : UndoableAction {
    SoloProcessor& processor;
    std::shared_ptr<const T> before, after;
    void (SoloProcessor::*restore)(std::shared_ptr<const T>);
    bool inPlace = true;
    SnapshotAction(SoloProcessor& p, std::shared_ptr<const T> b, std::shared_ptr<const T> a, void (SoloProcessor::*r)(std::shared_ptr<const T>))
        : processor(p), before(std::move(b)), after(std::move(a)), restore(r) {}
    bool perform() override { if (inPlace) { inPlace = false; return true; } (processor.*restore)(after); return true; }
    bool undo() override { inPlace = false; (processor.*restore)(before); return true; }
    int getSizeInUnits() override { return int(sizeof(T)) * 2; }
};

const char* kParamIds[] = { sid::channel, sid::model, sid::tempoSource, sid::tempo, sid::notesOnTick, sid::sound, sid::volume, sid::trim, sid::hexDisplay, sid::keyMap };
const char* kRowIds[] = { plugin::ids::instrument, plugin::ids::table, plugin::ids::level, plugin::ids::pan, plugin::ids::transpose,
                          plugin::ids::cmd1Type, plugin::ids::cmd1X, plugin::ids::cmd1Y, plugin::ids::cmd2Type, plugin::ids::cmd2X, plugin::ids::cmd2Y,
                          plugin::ids::liveFollow, plugin::ids::velocityMode, plugin::ids::keyswitch };
} // namespace

AudioProcessorValueTreeState::ParameterLayout SoloProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout L;
    L.add(std::make_unique<AudioParameterChoice>(ParameterID(sid::channel, 1), "Channel", StringArray{ "PU1", "PU2", "WAV", "NOI" }, 0));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID(sid::model, 1), "Model", StringArray{ "DMG", "CGB", "RAW" }, 0));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID(sid::tempoSource, 1), "Tempo Source", StringArray{ "Host", "Own" }, 0));
    L.add(intParam(sid::tempo, "Tempo", 40, 295, 120, [](int v, int) { return String(v) + " BPM"; }));
    L.add(std::make_unique<AudioParameterBool>(ParameterID(sid::notesOnTick, 1), "Quantize Notes To Ticks", false));
    L.add(intParam(sid::sound, "Sound", 0, kSoloSounds, 0, [](int v, int) { return v == 0 ? String("none") : String(v); }));
    L.add(intParam(sid::volume, "Volume", 0, 7, 7, [](int v, int) { return v == 0 ? String("0 (1/8)") : String(v); }));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID(sid::trim, 1), "Output Trim",
          NormalisableRange<float>(-40.0f, 6.0f, 0.1f), -6.0f,
          AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction([](float v, int) { return String(v, 1) + " dB"; })));
    L.add(std::make_unique<AudioParameterBool>(ParameterID(sid::hexDisplay, 1), "Hex Display", false));
    L.add(std::make_unique<AudioParameterBool>(ParameterID(sid::keyMap, 1), "Key Map", true));
    // The row: one lane set, every letter allowed since the channel can move;
    // Live follow on, so a change reaches the sounding note (plan section 2).
    addChannelParameters(L, sid::prefix, ChannelKind::Any, false, true);
    return L;
}

SoloProcessor::SoloProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ChipBoySolo", createLayout())
{
    params.bind(apvts, sid::prefix);
    pChannel_ = raw(sid::channel); pModel_ = raw(sid::model); pTempoSource_ = raw(sid::tempoSource); pTempo_ = raw(sid::tempo);
    pNotesOnTick_ = raw(sid::notesOnTick); pSound_ = raw(sid::sound); pVolume_ = raw(sid::volume); pTrim_ = raw(sid::trim); pKeyMap_ = raw(sid::keyMap);
    events_.reserve(2048); writes_.reserve(8192);
    cycleAt_ = [this](uint64_t f) { return renderer_.cycleForFrame(f); };
    // `new T(prvalue)` builds the bank in its heap block: 41 KB never sits
    // on the message thread's stack (Windows gives it a megabyte).
    publishBank(std::shared_ptr<const bank::Bank>(new bank::Bank(bank::Bank::factory())));
    auto s = std::make_shared<SoloState>();
    soloDefaultKeyMap(0, s->keyMap);
    publishSolo(std::move(s));
    startTimer(kTimerMs);
}

SoloProcessor::~SoloProcessor() { stopTimer(); }

/* -------------------------------------------------------- publishing */

void SoloProcessor::publishBank(std::shared_ptr<const bank::Bank> b)
{
    if (!b) return;
    if (bankShared_) retired_.push_back(bankShared_);
    while (retired_.size() > 32) retired_.pop_front();
    bankShared_ = std::move(b);
    bankPtr_.store(bankShared_.get(), std::memory_order_release);
}

void SoloProcessor::publishSolo(std::shared_ptr<const SoloState> s)
{
    if (!s) return;
    if (soloShared_) retired_.push_back(soloShared_);
    while (retired_.size() > 32) retired_.pop_front();
    soloShared_ = std::move(s);
    soloPtr_.store(soloShared_.get(), std::memory_order_release);
}

void SoloProcessor::restoreBank(std::shared_ptr<const bank::Bank> b) { publishBank(std::move(b)); }
void SoloProcessor::restoreSolo(std::shared_ptr<const SoloState> s) { publishSolo(std::move(s)); }

void SoloProcessor::editBank(const String& name, const std::function<void(bank::Bank&)>& fn)
{
    auto copy = std::make_shared<bank::Bank>();
    if (bankShared_) *copy = *bankShared_;
    fn(*copy);
    auto before = bankShared_;
    publishBank(std::shared_ptr<const bank::Bank>(std::move(copy)));
    history_.perform(std::make_unique<SnapshotAction<bank::Bank>>(*this, before, bankShared_, &SoloProcessor::restoreBank), name);
}

void SoloProcessor::editSolo(const String& name, const std::function<void(SoloState&)>& fn)
{
    auto copy = std::make_shared<SoloState>();
    if (soloShared_) *copy = *soloShared_;
    fn(*copy);
    auto before = soloShared_;
    publishSolo(std::shared_ptr<const SoloState>(std::move(copy)));
    history_.perform(std::make_unique<SnapshotAction<SoloState>>(*this, before, soloShared_, &SoloProcessor::restoreSolo), name);
}

void SoloProcessor::loadBankEdit(const String& name, const bank::Bank& b)
{
    auto copy = std::make_shared<bank::Bank>();
    *copy = b;
    auto before = bankShared_;
    publishBank(std::shared_ptr<const bank::Bank>(std::move(copy)));
    history_.perform(std::make_unique<SnapshotAction<bank::Bank>>(*this, before, bankShared_, &SoloProcessor::restoreBank), name);
}

/* ------------------------------------------------------------ sounds */

void SoloProcessor::storeSound(int slot, const String& name)
{
    if (slot < 1 || slot > kSoloSounds) return;
    const driver::ChannelParams row = params.read(ChannelKind::Any);
    const auto b = bankShared_;
    String n = name;
    if (n.isEmpty()) {
        const auto s = soloShared_;
        if (s && s->sounds[size_t(slot - 1)].used) n = String(CharPointer_UTF8(s->sounds[size_t(slot - 1)].name.c_str()));
        else if (const bank::Instrument* i = b ? b->instrument(row.instrument) : nullptr; i != nullptr && !i->name.empty()) n = String(CharPointer_UTF8(i->name.c_str()));
        else n = "Sound " + String(slot);
    }
    editSolo("Store sound " + String(slot), [&](SoloState& s) {
        auto& snd = s.sounds[size_t(slot - 1)];
        snd.used = true;
        snd.name = n.toStdString();
        snd.inst = row.instrument; snd.table = row.table;
        snd.cmd1 = row.cmd[0]; snd.cmd2 = row.cmd[1];
    });
}

void SoloProcessor::recallSound(int slot, bool byHand)
{
    const auto s = soloShared_;
    if (!s || slot < 1 || slot > kSoloSounds) return;
    const SoloSound& snd = s->sounds[size_t(slot - 1)];
    if (!snd.used) return;
    auto set = [&](const String& id, float v) {
        auto* p = apvts.getParameter(id);
        if (p == nullptr) return;
        if (byHand) history_.setParameter(*p, v);
        else if (std::abs(p->convertFrom0to1(p->getValue()) - v) > 1.0e-4f) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture(); }
    };
    const String what = "Recall sound " + String(slot) + (snd.name.empty() ? String() : " " + String(CharPointer_UTF8(snd.name.c_str())));
    if (byHand) history_.beginGesture(what);
    set(channelParamId(0, plugin::ids::instrument), float(snd.inst));
    set(channelParamId(0, plugin::ids::table), float(snd.table));
    set(channelParamId(0, plugin::ids::cmd1Type), float(choiceFromCmd(snd.cmd1.cmd)));
    set(channelParamId(0, plugin::ids::cmd1X), float(snd.cmd1.a));
    set(channelParamId(0, plugin::ids::cmd1Y), float(snd.cmd1.b));
    set(channelParamId(0, plugin::ids::cmd2Type), float(choiceFromCmd(snd.cmd2.cmd)));
    set(channelParamId(0, plugin::ids::cmd2X), float(snd.cmd2.a));
    set(channelParamId(0, plugin::ids::cmd2Y), float(snd.cmd2.b));
    // The Sound parameter names what was recalled, whoever recalled it.
    set(sid::sound, float(slot));
    if (byHand) { history_.endGesture(); lastRecall_ = what; ++recallSerial_; }
}

void SoloProcessor::useLibraryCommand(int entry, int slot)
{
    const auto s = soloShared_;
    if (!s || entry < 1 || entry > kSoloCommands) return;
    const SoloCommand& c = s->commands[size_t(entry - 1)];
    if (!c.used) return;
    const char* typeId = slot == 0 ? plugin::ids::cmd1Type : plugin::ids::cmd2Type;
    const char* xId = slot == 0 ? plugin::ids::cmd1X : plugin::ids::cmd2X;
    const char* yId = slot == 0 ? plugin::ids::cmd1Y : plugin::ids::cmd2Y;
    history_.beginGesture("CMD " + String(slot + 1) + " from the library");
    history_.setParameter(*apvts.getParameter(channelParamId(0, typeId)), float(choiceFromCmd(c.cmd.cmd)));
    history_.setParameter(*apvts.getParameter(channelParamId(0, xId)), float(c.cmd.a));
    history_.setParameter(*apvts.getParameter(channelParamId(0, yId)), float(c.cmd.b));
    history_.endGesture();
}

void SoloProcessor::storeLibraryCommand(int entry, int slot, const String& name)
{
    if (entry < 1 || entry > kSoloCommands) return;
    const driver::ChannelParams row = params.read(ChannelKind::Any);
    const bank::Command cmd = row.cmd[slot & 1];
    editSolo("Store command " + String(entry), [&](SoloState& s) {
        auto& c = s.commands[size_t(entry - 1)];
        c.used = true;
        c.cmd = cmd;
        if (name.isNotEmpty()) c.name = name.toStdString();
        else if (c.name.empty()) c.name = cmd.cmd == bank::Cmd::None ? std::string("none") : std::string(bank::cmdLetter(cmd.cmd)) + " " + commandArgText(cmd).toStdString();
    });
}

/// The audio thread's recall (plan section 2): the row the driver reads is
/// the sound's from this block on -- so a note in the same block sounds with
/// it -- and the timer brings the parameters up to it.
void SoloProcessor::recallOnAudioThread(int slot, const SoloState* s, int key)
{
    if (s == nullptr || slot < 1 || slot > kSoloSounds) return;
    const SoloSound& snd = s->sounds[size_t(slot - 1)];
    if (!snd.used) return;
    overrideOn_ = true; overrideBlocks_ = 0;
    overrideInst_ = snd.inst; overrideTable_ = snd.table; overrideCmd_[0] = snd.cmd1; overrideCmd_[1] = snd.cmd2;
    if (key >= 0) lastKey_.store(key);
    recallReq_.store(uint32_t(slot & 0xFF) | (uint32_t((key + 1) & 0xFF) << 8) | (uint32_t(++recallSerialAudio_ & 0xFFFF) << 16), std::memory_order_release);
}

void SoloProcessor::timerCallback()
{
    const uint32_t req = recallReq_.load(std::memory_order_acquire);
    if (req != recallSeen_) {
        recallSeen_ = req;
        const int slot = int(req & 0xFF), key = int((req >> 8) & 0xFF) - 1;
        recallSound(slot, false);
        recallApplied_.store(req >> 16, std::memory_order_release);   // the parameters carry the sound now
        const auto s = soloShared_;
        String what = "Sound " + String(slot);
        if (s && s->sounds[size_t(slot - 1)].used && !s->sounds[size_t(slot - 1)].name.empty()) what += " " + String(CharPointer_UTF8(s->sounds[size_t(slot - 1)].name.c_str()));
        if (key >= 0) what += String(CharPointer_UTF8(" \xc2\xb7 key ")) + ui::ValueFormat::noteName(key);
        else what += " (parameter)";
        lastRecall_ = what;
        ++recallSerial_;
    }
}

/* ------------------------------------------------------------- files */

File SoloProcessor::soloFolder()
{
    const File f = File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("ChipBoy").getChildFile("Solo");
    f.createDirectory();
    return f;
}

bool SoloProcessor::saveSoloFile(const File& file) const
{
    auto* o = new DynamicObject();
    o->setProperty("format", "chipboy-solo");
    o->setProperty("version", 1);
    o->setProperty("channel", channel());
    if (bankShared_) o->setProperty("bank", bankToVar(*bankShared_));
    if (soloShared_) o->setProperty("solo", soloStateToVar(*soloShared_));
    auto* pv = new DynamicObject();
    for (const char* id : kParamIds) if (auto* p = apvts.getParameter(id)) pv->setProperty(id, p->convertFrom0to1(p->getValue()));
    for (const char* id : kRowIds) { const String full = channelParamId(0, id); if (auto* p = apvts.getParameter(full)) pv->setProperty(full, p->convertFrom0to1(p->getValue())); }
    o->setProperty("params", var(pv));
    return file.replaceWithText(JSON::toString(var(o), false));
}

bool SoloProcessor::loadSoloFile(const File& file, String& report)
{
    var v;
    if (JSON::parse(file.loadFileAsString(), v).failed() || v.getDynamicObject() == nullptr) { report = "not a ChipBoy Solo file"; return false; }
    auto* o = v.getDynamicObject();
    if (o->getProperty("format").toString() != "chipboy-solo") { report = "not a ChipBoy Solo file"; return false; }
    auto b = std::make_shared<bank::Bank>();
    const bool hasBank = o->hasProperty("bank") && bankFromVar(o->getProperty("bank"), *b);
    auto s = std::make_shared<SoloState>();
    const bool hasSolo = o->hasProperty("solo") && soloStateFromVar(o->getProperty("solo"), *s);
    const String what = "Load " + file.getFileNameWithoutExtension();
    history_.beginGesture(what);
    if (hasBank) { auto before = bankShared_; publishBank(std::shared_ptr<const bank::Bank>(std::move(b))); history_.perform(std::make_unique<SnapshotAction<bank::Bank>>(*this, before, bankShared_, &SoloProcessor::restoreBank), what); }
    if (hasSolo) { auto before = soloShared_; publishSolo(std::shared_ptr<const SoloState>(std::move(s))); history_.perform(std::make_unique<SnapshotAction<SoloState>>(*this, before, soloShared_, &SoloProcessor::restoreSolo), what); }
    if (auto* pv = o->getProperty("params").getDynamicObject())
        for (const auto& prop : pv->getProperties())
            if (auto* p = apvts.getParameter(prop.name.toString())) history_.setParameter(*p, float(double(prop.value)));
    history_.endGesture();
    report = file.getFileName() + (hasBank ? ": bank" : ": no bank") + (hasSolo ? ", sounds and keys" : ", no sounds");
    return hasBank || hasSolo;
}

/* ------------------------------------------------------------- audio */

void SoloProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    sampleRate_ = sampleRate;
    blockSize_ = std::max(16, samplesPerBlock);
    modelIndex_ = -1;
    const int model = paramInt(pModel_);
    const Console console = model == 1 ? Console::CGB : Console::DMG;
    renderer_.prepare(sampleRate, AnalogModel::forConsole(console), std::max(blockSize_, 4096));
    driver_.prepare(sampleRate, bankPtr_.load(), nullptr, console);
    clock_.prepare(sampleRate);
    apu_.reset();
    frames_ = 0;
    events_.clear(); writes_.clear();
    scopes_.master.sampleRate = sampleRate;
    prevChannel_ = -1;
    applyModel();
    // No noise floor, no display line, no departures: the channel and its analog stage.
    render::Renderer::Options o;
    o.noise = false; o.lcd = false; o.bassMod = 0; o.declickMs = 0.0f; o.softenMaster = false;
    renderer_.setOptions(o);
    setLatencySamples(renderer_.latencyFrames());
}

bool SoloProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono();
}

void SoloProcessor::applyModel()
{
    const int model = paramInt(pModel_);
    if (model == modelIndex_) return;
    modelIndex_ = model;
    const Console console = model == 1 ? Console::CGB : Console::DMG;
    renderer_.setModel(AnalogModel::forConsole(console), /*bypassAnalog*/ model == 2);
    driver_.setModel(console);
    apu_.setModel(console);
}

void SoloProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (n <= 0) return;

    const auto* bankNow = bankPtr_.load(std::memory_order_acquire);
    const auto* soloNow = soloPtr_.load(std::memory_order_acquire);
    driver_.setBank(bankNow);
    applyModel();
    const int ch = channel();
    events_.clear();
    // The channel moved: nothing may ring on the one it left (section 9.1).
    if (prevChannel_ >= 0 && ch != prevChannel_) {
        driver::NoteEvent e; e.kind = driver::NoteEvent::AllNotesOff; e.channel = uint8_t(prevChannel_); e.offset = 0;
        events_.push_back(e);
    }
    prevChannel_ = ch;
    driver_.setGateMask(1u << ch);
    driver::GlobalParams g;
    g.masterL = g.masterR = uint8_t(std::clamp(paramInt(pVolume_, 7), 0, 7));
    driver_.setGlobal(g);
    driver_.setNotesOnTick(paramInt(pNotesOnTick_) != 0);
    driver_.setRecordMask(0);

    // --- the clock: the host's beat, or the own tempo; free-running when
    // stopped, so tables and commands run while playing live ----------------
    driver::Transport t;
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            t.playing = pos->getIsPlaying();
            const auto bpm = pos->getBpm(); const auto ppq = pos->getPpqPosition();
            t.valid = bpm.hasValue() && ppq.hasValue();
            t.bpm = bpm.orFallback(120.0); t.ppq = ppq.orFallback(0.0);
            if (const auto secs = pos->getTimeInSeconds()) { t.seconds = *secs; t.timeValid = true; }
        }
    }
    if (t.valid && !t.timeValid && t.bpm > 1.0) { t.seconds = t.ppq * 60.0 / t.bpm; t.timeValid = true; }
    clock_.setOwnsTransport(!t.valid);
    driver::ClockConfig cc;
    const bool own = paramInt(pTempoSource_) != 0 || clock_.ownsTransport();
    cc.source = own ? driver::TempoSource::Song : driver::TempoSource::Host;
    cc.songTempo = double(std::clamp(paramInt(pTempo_, 120), 40, 295));
    cc.songStartSeconds = 0.0;
    cc.lsdjTempo = false;
    // A T in a slot is the tempo from now on, as in the main plugin (section 4).
    for (int i = 0; i < 2; ++i) {
        const auto& c = driver_.slot(ch, i);
        if (c.cmd == bank::Cmd::T && !bank::isRevert(c)) { cc.songTempo = double(std::clamp<int>(c.a, 40, 295)); break; }
    }
    clock_.setConfig(cc);
    clock_.setTempoMap(nullptr, 0);
    clock_.setLoop(false, 0, 0);
    clock_.process(t, uint32_t(n), frames_);
    playing_.store(clock_.playing());
    hostTempo_.store(!own && t.valid);
    tempo_.store(clock_.bpm());

    // --- MIDI: every channel of the track to this voice; a mapped key
    // recalls a sound instead of sounding (plan section 2) -------------------
    const bool keys = paramInt(pKeyMap_) != 0 && soloNow != nullptr;
    int lo = 0, hi = 127;
    soloNoteRange(ch, lo, hi);
    for (const auto meta : midi) {
        const MidiMessage m = meta.getMessage();
        driver::NoteEvent e;
        e.offset = uint32_t(std::max(0, meta.samplePosition)); e.channel = uint8_t(ch); e.source = driver::NoteEvent::Midi;
        if (m.isNoteOn() || m.isNoteOff()) {
            const int note = m.getNoteNumber();
            if (keys && (note < lo || note > hi)) {
                const int slot = soloNow->keyMap[size_t(note & 127)];
                if (m.isNoteOn() && m.getVelocity() > 0 && slot >= 1) recallOnAudioThread(slot, soloNow, note);
                continue;                                  // a mapped key never sounds, nor does its release
            }
            if (m.isNoteOn()) { e.kind = driver::NoteEvent::NoteOn; e.a = uint8_t(note); e.b = uint8_t(m.getVelocity()); }
            else { e.kind = driver::NoteEvent::NoteOff; e.a = uint8_t(note); }
        }
        else if (m.isPitchWheel()) { e.kind = driver::NoteEvent::PitchBend; e.value = int16_t(m.getPitchWheelValue() - 8192); }
        else if (m.isController()) { e.kind = driver::NoteEvent::Control; e.a = uint8_t(m.getControllerNumber()); e.b = uint8_t(m.getControllerValue()); }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { e.kind = driver::NoteEvent::AllNotesOff; }
        else continue;
        events_.push_back(e);
    }
    // The Sound parameter moving -- a lane, or a hand -- recalls too.
    const int soundParam = paramInt(pSound_);
    if (lastSoundParam_ >= 0 && soundParam != lastSoundParam_ && soundParam >= 1) recallOnAudioThread(soundParam, soloNow, -1);
    lastSoundParam_ = soundParam;

    // --- the row -------------------------------------------------------------
    driver::ChannelParams row = params.read(ChannelKind::Any);
    if (ch == 2 && row.level != 255) row.level = uint8_t(row.level == 0 ? 0 : row.level <= 5 ? 1 : row.level <= 10 ? 2 : 3);   // sixteen levels onto the wave channel's four
    if (overrideOn_) {
        // Until the timer has written the sound into the parameters, the row
        // the driver reads is the sound's; after that the parameters carry it.
        const bool written = (recallApplied_.load(std::memory_order_acquire) & 0xFFFFu) == (recallSerialAudio_ & 0xFFFFu);
        if (written || ++overrideBlocks_ > kOverrideBlocks) overrideOn_ = false;
        else { row.instrument = overrideInst_; row.table = overrideTable_; row.cmd[0] = overrideCmd_[0]; row.cmd[1] = overrideCmd_[1]; }
    }
    driver_.setParams(ch, row);
    driver_.setViewGroove(ch, tracker::kGrooveNone);
    driver_.setTableGroove(ch, nullptr);

    std::stable_sort(events_.begin(), events_.end(), [](const driver::NoteEvent& a, const driver::NoteEvent& b) {
        auto rank = [](const driver::NoteEvent& e) { return e.kind == driver::NoteEvent::AllNotesOff ? 0 : e.kind == driver::NoteEvent::NoteOff || (e.kind == driver::NoteEvent::NoteOn && e.b == 0) ? 1 : 2; };
        return a.offset != b.offset ? a.offset < b.offset : rank(a) < rank(b);
    });
    for (const auto& e : events_) {
        if (e.kind == driver::NoteEvent::NoteOn && e.b) lastNote_.store(e.a);
        else if (e.kind == driver::NoteEvent::NoteOff || (e.kind == driver::NoteEvent::NoteOn && !e.b)) { if (lastNote_.load() == e.a) lastNote_.store(-1); }
    }

    writes_.clear();
    driver_.setTickRate(clock_.bpm() * double(driver::kTicksPerBeat) / 60.0);
    driver_.process(events_.data(), events_.size(), uint32_t(n), frames_, clock_.ticks(), clock_.tickCount(), cycleAt_, writes_);
    applyWrites();
    apu_.runTo(std::max(renderer_.cycleForFrame(frames_ + uint64_t(n)), apu_.cycle()));

    float* L = buffer.getWritePointer(0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    tapScopes(n);
    if (R) renderer_.render(apu_, L, R, n);
    else renderer_.render(apu_, L, L, n);
    scopes_.master.push(L, R, uint32_t(n));
    mixPreview(L, R ? R : L, n);
    const float gain = 0.25f * Decibels::decibelsToGain(pTrim_ ? pTrim_->load() : -6.0f);
    buffer.applyGain(gain);
    frames_ += uint64_t(n);
}

void SoloProcessor::applyWrites()
{
    for (const auto& w : writes_) {
        apu_.runTo(std::max(w.cycle, apu_.cycle()));
        apu_.write(w.addr, w.value);
    }
}

void SoloProcessor::tapScopes(int n)
{
    for (const auto& e : apu_.events()) scopes_.channels[e.channel & 3].push(e.cycle, e.dacOn ? int(e.level) : -1);
    scopes_.latestCycle.store(apu_.cycle(), std::memory_order_release);
    scopes_.latestFrame.store(frames_ + uint64_t(n), std::memory_order_release);
    for (int ch = 0; ch < 4; ++ch) {
        scopes_.state[size_t(ch)].store(link::packState(driver_.view(ch)), std::memory_order_release);
        scopes_.state2[size_t(ch)].store(link::packState2(driver_.view(ch)), std::memory_order_release);
        scopes_.tableRun[size_t(ch)].store(packTableRun(driver_.view(ch)), std::memory_order_release);
        scopes_.tableLanes[size_t(ch)].store(packTableLanes(driver_.view(ch)), std::memory_order_release);
    }
    scopes_.mix.store(uint32_t(driver_.nr50()) | (uint32_t(driver_.nr51()) << 8) | (1u << 16), std::memory_order_release);
}

void SoloProcessor::previewKitSample(int slot, int sample)
{
    const uint32_t req = (uint32_t(slot & 63) << 8) | uint32_t(sample & 63) | (uint32_t(++previewSeq_ & 0x3FFFF) << 14);
    previewReq_.store(req, std::memory_order_release);
}

void SoloProcessor::mixPreview(float* left, float* right, int n)
{
    // D-UI-29: the Kits tab's audition, as the main plugin mixes it.
    const uint32_t req = previewReq_.load(std::memory_order_acquire);
    if (req != previewSeen_) {
        previewSeen_ = req;
        previewSlot_ = int((req >> 8) & 63); previewIdx_ = int(req & 63);
        previewPos_ = 0.0; previewOn_ = previewSlot_ > 0;
    }
    if (!previewOn_) return;
    const auto* b = bankPtr_.load(std::memory_order_acquire);
    const bank::Kit* k = b ? b->kit(previewSlot_) : nullptr;
    if (!k || previewIdx_ < 0 || previewIdx_ >= int(k->samples.size())) { previewOn_ = false; return; }
    const auto& data = k->samples[size_t(previewIdx_)].data;
    if (data.empty() || sampleRate_ <= 0.0) { previewOn_ = false; return; }
    const double step = bank::sampleRateForPeriod(k->period) / sampleRate_;
    for (int i = 0; i < n; ++i) {
        const size_t at = size_t(previewPos_);
        if (at >= data.size()) { previewOn_ = false; break; }
        const float dac = -(float(data[at]) - 7.5f) / 7.5f;
        left[i] += dac; if (right != left) right[i] += dac;
        previewPos_ += step;
    }
}

/* ------------------------------------------------------------- state */

AudioProcessorEditor* SoloProcessor::createEditor() { return new SoloEditor(*this); }

void SoloProcessor::getStateInformation(MemoryBlock& dest)
{
    ValueTree root("ChipBoySoloState");
    root.setProperty("version", 1, nullptr);
    root.addChild(apvts.copyState(), -1, nullptr);
    if (bankShared_) root.setProperty("bank", bankToJson(*bankShared_), nullptr);
    if (soloShared_) root.setProperty("solo", JSON::toString(soloStateToVar(*soloShared_), true), nullptr);
    MemoryOutputStream mo(dest, false);
    root.writeToStream(mo);
}

void SoloProcessor::setStateInformation(const void* data, int size)
{
    const ValueTree root = ValueTree::readFromData(data, size_t(size));
    if (!root.isValid() || !root.hasType("ChipBoySoloState")) return;
    history_.clear();                                    // a project loading is not an edit
    const ValueTree p = root.getChildWithName(apvts.state.getType());
    if (p.isValid()) apvts.replaceState(p);
    {
        auto b = std::make_shared<bank::Bank>();
        if (root.hasProperty("bank") && bankFromJson(root["bank"].toString(), *b)) publishBank(std::shared_ptr<const bank::Bank>(std::move(b)));
    }
    {
        auto s = std::make_shared<SoloState>();
        var v;
        if (root.hasProperty("solo") && !JSON::parse(root["solo"].toString(), v).failed() && soloStateFromVar(v, *s)) publishSolo(std::shared_ptr<const SoloState>(std::move(s)));
    }
    lastSoundParam_ = -1;                                // the restored Sound value is where it stands, not a recall
}

} // namespace chipboy::plugin

#ifndef CHIPBOY_NO_PLUGIN_FILTER
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chipboy::plugin::SoloProcessor();
}
#endif
