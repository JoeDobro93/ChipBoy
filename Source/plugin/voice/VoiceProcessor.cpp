#include "plugin/voice/VoiceProcessor.h"

#include "plugin/shared/BankJson.h"
#include "plugin/voice/VoiceEditor.h"

#include <mutex>
#include <set>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::link;

namespace {
constexpr const char* kPrefix = "v_";
constexpr const char* kSourceId = "inst_source";
constexpr int kTimerMs = 100;
constexpr int kParamsRefreshBlocks = 32;

// Voices alive in this process, by UUID: a duplicated track copies its
// state, and two Voices with one UUID would fight over a claim (11.5).
std::mutex& registryMutex() { static std::mutex m; return m; }
std::set<String>& registry() { static std::set<String> s; return s; }
bool registerUuid(const String& u) { std::lock_guard<std::mutex> l(registryMutex()); return registry().insert(u).second; }
void unregisterUuid(const String& u) { std::lock_guard<std::mutex> l(registryMutex()); registry().erase(u); }
}

AudioProcessorValueTreeState::ParameterLayout VoiceProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout L;
    L.add(std::make_unique<AudioParameterChoice>(ParameterID(kSourceId, 1), "Instrument Source", StringArray { "linked", "local" }, 0));
    addChannelParameters(L, kPrefix, ChannelKind::Any, false);
    return L;
}

VoiceProcessor::VoiceProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "ChipBoyVoice", createLayout()),
      uuid_(Uuid().toString())
{
    params.bind(apvts, kPrefix);
    pSource_ = apvts.getRawParameterValue(kSourceId);
    local_ = bank::Instrument::defaults(bank::InstrumentType::Pulse, "Local");
    local_.used = true;
    registerUuid(uuid_);
    startTimer(kTimerMs);
}

VoiceProcessor::~VoiceProcessor()
{
    stopTimer();
    link_.release();
    unregisterUuid(uuid_);
}

void VoiceProcessor::prepareToPlay(double, int) {}

bool VoiceProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::stereo() || out == AudioChannelSet::mono() || out.isDisabled();
}

void VoiceProcessor::updateTrackProperties(const TrackProperties& p)
{
    if (p.name.has_value() && p.name->isNotEmpty()) trackName_ = *p.name;
}

void VoiceProcessor::setTarget(const String& uuid, int channel)
{
    link_.setTarget(uuid, channel);
    haveLastParams_ = false;
    localDirty_ = true;
}

void VoiceProcessor::localChanged() { localDirty_ = true; }

namespace {
/// The Voice's local instrument, before and after a Pull: the one thing in
/// this window that is not a parameter, so it needs its own action
/// (UI_DESIGN section 2.1).
struct LocalInstrumentAction : juce::UndoableAction {
    VoiceProcessor& processor;
    bank::Instrument before, after;
    bool inPlace = true;

    LocalInstrumentAction(VoiceProcessor& p, bank::Instrument b, bank::Instrument a)
        : processor(p), before(std::move(b)), after(std::move(a)) {}

    bool perform() override
    {
        if (inPlace) { inPlace = false; return true; }
        processor.restoreLocalInstrument(after);
        return true;
    }
    bool undo() override
    {
        inPlace = false;
        processor.restoreLocalInstrument(before);
        return true;
    }
    int getSizeInUnits() override { return int(sizeof(bank::Instrument)) * 2; }
};
} // namespace

void VoiceProcessor::restoreLocalInstrument(const bank::Instrument& i)
{
    {
        std::lock_guard<std::mutex> lock(localMutex_);
        local_ = i;
        local_.used = true;
    }
    localDirty_ = true;
}

void VoiceProcessor::publishLocal()
{
    auto* s = link_.slot();
    if (!s) return;
    bank::InstrumentCore core;
    {
        std::lock_guard<std::mutex> lock(localMutex_);
        core = static_cast<const bank::InstrumentCore&>(local_);
    }
    s->local.write(core);
    s->useLocal.store(usingLocal() ? 1u : 0u, std::memory_order_release);
}

void VoiceProcessor::pushToSlot(int slot1)
{
    auto* s = link_.slot();
    if (!s || slot1 < 1 || slot1 > 128) { requestMessage_ = "not linked"; return; }
    if (pendingRequest_ != Request::None) { requestMessage_ = "previous request still pending"; return; }
    {
        std::lock_guard<std::mutex> lock(localMutex_);
        s->exchange.write(static_cast<const bank::InstrumentCore&>(local_));
        setString(s->exchangeName, kInstNameChars, local_.name.c_str());
    }
    pendingSlot_ = slot1;
    pendingRequest_ = Request::PushToSlot;
    pendingSerial_ = s->requestSerial.load() + 1;
    s->requestSlot.store(uint32_t(slot1));
    s->request.store(uint32_t(Request::PushToSlot));
    s->requestSerial.store(pendingSerial_, std::memory_order_release);
    requestMessage_ = "pushing to slot " + String(slot1) + "...";
}

void VoiceProcessor::pullFromSlot(int slot1)
{
    auto* s = link_.slot();
    if (!s || slot1 < 1 || slot1 > 128) { requestMessage_ = "not linked"; return; }
    if (pendingRequest_ != Request::None) { requestMessage_ = "previous request still pending"; return; }
    pendingSlot_ = slot1;
    pendingRequest_ = Request::PullFromSlot;
    pendingSerial_ = s->requestSerial.load() + 1;
    s->requestSlot.store(uint32_t(slot1));
    s->request.store(uint32_t(Request::PullFromSlot));
    s->requestSerial.store(pendingSerial_, std::memory_order_release);
    requestMessage_ = "pulling slot " + String(slot1) + "...";
}

void VoiceProcessor::requestFocus()
{
    if (auto* s = link_.slot()) s->focusRequest.store(1u);
}

driver::VoiceView VoiceProcessor::view() const
{
    driver::VoiceView v;
    if (auto* s = link_.slot()) {
        unpackState(s->state.load(std::memory_order_acquire), v);
        unpackState2(s->state2.load(std::memory_order_acquire), v);
    }
    return v;
}

uint32_t VoiceProcessor::scopeSnapshot(ScopeSample* out, uint32_t count) const
{
    auto* s = link_.slot();
    return s ? s->scope.snapshot(out, count) : 0;
}

double VoiceProcessor::linkedSampleRate() const
{
    auto* r = link_.region();
    return r ? double(r->sampleRate.load()) : 0.0;
}

void VoiceProcessor::timerCallback()
{
    // Claims, heartbeats and status. A directory scan every second.
    link_.maintain(uuid_, displayName());
    if (++ticks_ % 10 == 0) link_.scan();

    auto* s = link_.slot();
    if (s) {
        if (localDirty_.exchange(false)) publishLocal();
        else if ((s->useLocal.load() != 0) != usingLocal()) s->useLocal.store(usingLocal() ? 1u : 0u);

        if (pendingRequest_ != Request::None && s->ackSerial.load(std::memory_order_acquire) == pendingSerial_) {
            const bool ok = s->ackResult.load() == 0;
            if (pendingRequest_ == Request::PullFromSlot && ok) {
                bank::InstrumentCore core;
                bank::Instrument before, after;
                bool replaced = false;
                if (s->exchange.read(core)) {
                    std::lock_guard<std::mutex> lock(localMutex_);
                    before = local_;
                    static_cast<bank::InstrumentCore&>(local_) = core;
                    char n[kInstNameChars]; safeString(s->exchangeName, kInstNameChars, n, kInstNameChars);
                    local_.name = n;
                    local_.used = true;
                    after = local_;
                    replaced = true;
                }
                localDirty_ = true;
                requestMessage_ = "pulled slot " + String(pendingSlot_);
                // A pull replaces the local instrument, so it is an edit.
                if (replaced)
                    history_.perform(std::make_unique<LocalInstrumentAction>(*this, before, after),
                                     "Pull slot " + String(pendingSlot_) + " into the local instrument");
            } else if (pendingRequest_ == Request::PushToSlot) {
                requestMessage_ = ok ? "pushed to slot " + String(pendingSlot_) : "the ChipBoy instance refused the push";
            } else {
                requestMessage_ = "the ChipBoy instance refused the pull";
            }
            s->request.store(uint32_t(Request::None));
            pendingRequest_ = Request::None;
        }
    } else if (pendingRequest_ != Request::None) {
        pendingRequest_ = Request::None;
        requestMessage_ = "link lost before the request completed";
    }
}

void VoiceProcessor::processBlock(AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    buffer.clear();                       // silence: the audio is on the ChipBoy track (11.6)
    const int n = buffer.getNumSamples();
    auto* s = link_.slot();
    if (!s || n <= 0) { haveLastParams_ = false; return; }

    uint64_t hostFrame = kNoHostFrame;
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto t = pos->getTimeInSamples()) if (*t >= 0) hostFrame = uint64_t(*t);

    const uint8_t ch = uint8_t(link_.targetChannel() & 3);

    // Parameters: a snapshot when they change, and periodically so a main
    // instance that appears later catches up.
    const driver::ChannelParams p = params.read(ChannelKind::Any);
    const bool changed = !haveLastParams_ || std::memcmp(&p, &lastParams_, sizeof(p)) != 0;
    if (changed || ++blocksSinceParams_ >= kParamsRefreshBlocks) {
        LinkEvent e; e.kind = LinkEvent::Params; e.hostFrame = hostFrame; e.offset = 0; e.blockSize = uint32_t(n);
        e.params = p; e.note.channel = ch;
        if (s->events.push(e)) { lastParams_ = p; haveLastParams_ = true; blocksSinceParams_ = 0; }
    }

    for (const auto meta : midi) {
        const MidiMessage m = meta.getMessage();
        driver::NoteEvent ev;
        ev.offset = uint32_t(std::max(0, meta.samplePosition)); ev.channel = ch; ev.source = driver::NoteEvent::Midi;
        if (m.isNoteOn()) { ev.kind = driver::NoteEvent::NoteOn; ev.a = uint8_t(m.getNoteNumber()); ev.b = uint8_t(m.getVelocity()); }
        else if (m.isNoteOff()) { ev.kind = driver::NoteEvent::NoteOff; ev.a = uint8_t(m.getNoteNumber()); }
        else if (m.isPitchWheel()) { ev.kind = driver::NoteEvent::PitchBend; ev.value = int16_t(m.getPitchWheelValue() - 8192); }
        else if (m.isController()) { ev.kind = driver::NoteEvent::Control; ev.a = uint8_t(m.getControllerNumber()); ev.b = uint8_t(m.getControllerValue()); }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { ev.kind = driver::NoteEvent::AllNotesOff; }
        else continue;
        LinkEvent e; e.kind = LinkEvent::Note; e.hostFrame = hostFrame; e.offset = ev.offset; e.blockSize = uint32_t(n); e.note = ev;
        s->events.push(e);
    }
}

AudioProcessorEditor* VoiceProcessor::createEditor()
{
    return new VoiceEditor(*this);
}

void VoiceProcessor::getStateInformation(MemoryBlock& dest)
{
    ValueTree root("ChipBoyVoiceState");
    root.setProperty("version", 1, nullptr);
    root.setProperty("uuid", uuid_, nullptr);
    root.setProperty("name", trackName_, nullptr);
    root.setProperty("target", link_.targetUuid(), nullptr);
    root.setProperty("channel", link_.targetChannel(), nullptr);
    {
        std::lock_guard<std::mutex> lock(localMutex_);
        root.setProperty("local", instrumentToJson(local_), nullptr);
    }
    root.addChild(apvts.copyState(), -1, nullptr);
    MemoryOutputStream mo(dest, false);
    root.writeToStream(mo);
}

void VoiceProcessor::setStateInformation(const void* data, int size)
{
    // A project loading is not an edit (UI_DESIGN section 2.1).
    history_.clear();
    const ValueTree root = ValueTree::readFromData(data, size_t(size));
    if (!root.isValid() || !root.hasType("ChipBoyVoiceState")) return;
    if (root.hasProperty("uuid")) {
        const String saved = root["uuid"].toString();
        // Take the saved identity unless another live Voice already has it
        // (a duplicated track): then this one keeps its fresh UUID.
        if (saved.isNotEmpty() && saved != uuid_ && registerUuid(saved)) { unregisterUuid(uuid_); uuid_ = saved; }
    }
    if (root.hasProperty("name")) trackName_ = root["name"].toString();
    const ValueTree params_ = root.getChildWithName(apvts.state.getType());
    if (params_.isValid()) apvts.replaceState(params_);
    if (root.hasProperty("local")) {
        bank::Instrument i;
        if (instrumentFromJson(root["local"].toString(), i)) { std::lock_guard<std::mutex> lock(localMutex_); local_ = i; local_.used = true; }
    }
    setTarget(root.getProperty("target", "").toString(), int(root.getProperty("channel", 0)));
}

} // namespace chipboy::plugin

#ifndef CHIPBOY_NO_PLUGIN_FILTER
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chipboy::plugin::VoiceProcessor();
}
#endif
