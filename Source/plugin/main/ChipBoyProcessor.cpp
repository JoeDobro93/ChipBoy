#include "plugin/main/ChipBoyProcessor.h"

#include "plugin/main/ChipBoyEditor.h"

#include "plugin/shared/BankJson.h"

#include <algorithm>
#include <limits>

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
    pNoise_ = g(ids::noise); pLcd_ = g(ids::lcd); pBassMod_ = g(ids::bassMod);
    pDeclick_ = g(ids::declick); pDeclickMs_ = g(ids::declickMs); pSoften_ = g(ids::soften);
    pTempoSource_ = g(ids::tempoSource); pSongTempo_ = g(ids::songTempo); pNotesOnTick_ = g(ids::notesOnTick); pLink_ = g(ids::linkMode);

    events_.reserve(2048); delayed_.reserve(2048); writes_.reserve(8192);
    pendingLink_.reserve(kMaxPendingLink); pendingScratch_.reserve(kMaxPendingLink);
    cycleAt_ = [this](uint64_t f) { return renderer_.cycleForFrame(f); };

    // One empty tab to start with (section 18); everything below publishes
    // into it. `new T(prvalue)` builds the bank in its heap block; make_shared
    // would bind the prvalue to a reference first and leave 41 KB on the stack.
    tabs_.push_back(SongTab{});
    tabs_.back().id = nextTabId_++;
    publishBank(std::shared_ptr<const bank::Bank>(new bank::Bank(bank::Bank::factory())));
    publishSong(std::make_shared<tracker::Song>());
    active().dirty = false;
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
    active().bank = bankShared_;
    active().dirty = true;
}

void ChipBoyProcessor::publishSong(std::shared_ptr<tracker::Song> s, bool fromFile)
{
    if (!s) return;
    // The Song tempo parameter is the song's base tempo (section 4). A song
    // arriving from a file brings its own, which becomes the parameter's
    // value; otherwise the parameter is stamped into the song, so whatever is
    // saved from here carries the tempo it was played at.
    if (fromFile) {
        if (auto* prm = apvts.getParameter(ids::songTempo))
            prm->setValueNotifyingHost(prm->getNormalisableRange().convertTo0to1(float(std::clamp(s->tempoBpm, 40.0, 255.0))));
    }
    s->tempoBpm = songTempoParam();
    tempoBase_ = s->tempoBpm;
    tracker::buildTempoMap(*s, s->tempoBpm);   // the T cells, at their ticks, for the clock
    if (songShared_) retired_.push_back(songShared_);
    while (retired_.size() > 32) retired_.pop_front();
    songShared_ = std::move(s);
    songPtr_.store(songShared_.get(), std::memory_order_release);
    active().song = songShared_;
    active().dirty = true;
}

void ChipBoyProcessor::mutateBank(const std::function<void(bank::Bank&)>& fn)
{
    // Built empty in its heap block and then assigned: the conditional form
    // materialises a whole Bank on the caller's stack, and this runs on the
    // message thread, which a Windows host gives a megabyte.
    auto copy = std::make_shared<bank::Bank>();
    if (bankShared_) *copy = *bankShared_;
    fn(*copy);
    publishBank(std::move(copy));
}

void ChipBoyProcessor::mutateSong(const std::function<void(tracker::Song&)>& fn)
{
    auto copy = std::make_shared<tracker::Song>();          // on the heap, as mutateBank's is
    if (songShared_) *copy = *songShared_;
    fn(*copy);
    publishSong(std::move(copy));
}

/* ---------------------------------------------------------------- tabs */

juce::String ChipBoyProcessor::bankName() const { return active().bankName; }
juce::String ChipBoyProcessor::tabName(int i) const { return i >= 0 && i < tabCount() ? tabs_[size_t(i)].name : String(); }
File ChipBoyProcessor::tabFile(int i) const { return i >= 0 && i < tabCount() ? tabs_[size_t(i)].file : File(); }
bool ChipBoyProcessor::tabDirty(int i) const { return i >= 0 && i < tabCount() && tabs_[size_t(i)].dirty; }

int ChipBoyProcessor::tabIndexOfId(int id) const
{
    for (int i = 0; i < tabCount(); ++i) if (tabs_[size_t(i)].id == id) return i;
    return -1;
}

void ChipBoyProcessor::setSongTempoParam(double bpm)
{
    if (auto* prm = apvts.getParameter(ids::songTempo))
        prm->setValueNotifyingHost(prm->getNormalisableRange().convertTo0to1(float(std::clamp(bpm, 40.0, 255.0))));
}

/// The audio thread's two pointers, straight from a tab. Nothing is rebuilt:
/// the tempo map and the bar table were built when the song was published, on
/// the tempo the tab carries, which the parameter has just been given.
void ChipBoyProcessor::activate(const SongTab& tab)
{
    if (tab.bank) {
        if (bankShared_) retired_.push_back(bankShared_);
        bankShared_ = tab.bank;
        bankPtr_.store(bankShared_.get(), std::memory_order_release);
    }
    if (tab.song) {
        if (songShared_) retired_.push_back(songShared_);
        songShared_ = tab.song;
        songPtr_.store(songShared_.get(), std::memory_order_release);
    }
    while (retired_.size() > 32) retired_.pop_front();
    namesPublishedFor_ = nullptr;              // the link publishes this bank's names
}

void ChipBoyProcessor::setActiveTab(int i)
{
    if (i < 0 || i >= tabCount() || i == activeTab_) return;
    activeTab_ = i;
    const SongTab& tab = tabs_[size_t(i)];
    // The tab's master tempo becomes the parameter's value, and the map the
    // song already holds was built on it (section 19).
    tempoBase_ = tab.song ? tab.song->tempoBpm : 120.0;
    setSongTempoParam(tempoBase_);
    activate(tab);
    // A different piece: nothing of the old one is left ringing (section 18).
    flushRequest_.store(15);
}

int ChipBoyProcessor::newTab()
{
    auto song = std::make_shared<tracker::Song>();
    std::shared_ptr<const bank::Bank> factory(new bank::Bank(bank::Bank::factory()));
    return addTab(std::move(song), std::move(factory), "Song " + String(nextTabId_), "Factory");
}

int ChipBoyProcessor::addTab(std::shared_ptr<const tracker::Song> song, std::shared_ptr<const bank::Bank> bank,
                             const String& name, const String& bankName)
{
    SongTab tab;
    tab.id = nextTabId_++;
    tab.name = name.isNotEmpty() ? name : String("Song");
    tab.bankName = bankName;
    tab.song = song ? std::move(song) : std::shared_ptr<const tracker::Song>(new tracker::Song());
    tab.bank = bank ? std::move(bank) : std::shared_ptr<const bank::Bank>(new bank::Bank(bank::Bank::factory()));
    tabs_.push_back(std::move(tab));
    const int index = tabCount() - 1;
    // The song may never have been published, so its tempo map and bar table
    // are built now, on its own tempo, before anything plays it.
    {
        auto built = std::make_shared<tracker::Song>();
        *built = *tabs_[size_t(index)].song;
        tracker::buildTempoMap(*built, std::clamp(built->tempoBpm, 40.0, 255.0));
        tabs_[size_t(index)].song = std::move(built);
    }
    activeTab_ = index;
    tempoBase_ = tabs_[size_t(index)].song->tempoBpm;
    setSongTempoParam(tempoBase_);
    activate(tabs_[size_t(index)]);
    tabs_[size_t(index)].dirty = false;
    flushRequest_.store(15);
    return index;
}

bool ChipBoyProcessor::closeTab(int i)
{
    if (i < 0 || i >= tabCount() || tabCount() <= 1) return false;
    tabs_.erase(tabs_.begin() + i);
    const int want = std::clamp(activeTab_ > i ? activeTab_ - 1 : activeTab_, 0, tabCount() - 1);
    // Whatever is left has to be published: the closed tab may have been live.
    activeTab_ = want;
    tempoBase_ = active().song ? active().song->tempoBpm : 120.0;
    setSongTempoParam(tempoBase_);
    activate(active());
    flushRequest_.store(15);
    return true;
}

/* ---------------------------------------------------------------- undo */

namespace {

/// How much a bank snapshot costs the history: the bank itself plus the kit
/// samples it points at (UI_DESIGN section 2.1 -- the cap is in bytes).
int bankUnits(const bank::Bank* b)
{
    int64_t bytes = int64_t(sizeof(bank::Bank));
    if (b != nullptr)
        for (const auto& kit : b->kits)
            for (const auto& sample : kit.samples) bytes += int64_t(sample.data.size());
    return int(std::min<int64_t>(bytes, std::numeric_limits<int>::max()));
}

/// A bank the musician changed: the snapshot before and the snapshot after,
/// which are the copies the copy-on-write path already made. Constructed
/// with the change already in place, so the first perform() has nothing to
/// do; a redo after an undo does.
struct BankAction : juce::UndoableAction {
    ChipBoyProcessor& processor;
    int tabId;                      ///< the tab it belongs to, which it re-activates
    std::shared_ptr<const bank::Bank> before, after;
    juce::String nameBefore, nameAfter;
    bool inPlace = true;

    BankAction(ChipBoyProcessor& p, int tab, std::shared_ptr<const bank::Bank> b, juce::String nb,
               std::shared_ptr<const bank::Bank> a, juce::String na)
        : processor(p), tabId(tab), before(std::move(b)), after(std::move(a)), nameBefore(std::move(nb)), nameAfter(std::move(na)) {}

    bool perform() override
    {
        if (inPlace) { inPlace = false; return true; }
        processor.restoreBank(tabId, after, nameAfter);
        return true;
    }
    bool undo() override
    {
        inPlace = false;
        processor.restoreBank(tabId, before, nameBefore);
        return true;
    }
    int getSizeInUnits() override { return bankUnits(after.get()); }
};

/// The same for the song. A Song is the big one, about 300 KB.
struct SongAction : juce::UndoableAction {
    ChipBoyProcessor& processor;
    int tabId;                      ///< the tab it belongs to, which it re-activates
    std::shared_ptr<const tracker::Song> before, after;
    bool inPlace = true;

    SongAction(ChipBoyProcessor& p, int tab, std::shared_ptr<const tracker::Song> b, std::shared_ptr<const tracker::Song> a)
        : processor(p), tabId(tab), before(std::move(b)), after(std::move(a)) {}

    bool perform() override
    {
        if (inPlace) { inPlace = false; return true; }
        processor.restoreSong(tabId, after);
        return true;
    }
    bool undo() override
    {
        inPlace = false;
        processor.restoreSong(tabId, before);
        return true;
    }
    int getSizeInUnits() override { return int(sizeof(tracker::Song)); }
};

const char* channelShortName(int ch)
{
    static const char* names[] = { "PU1", "PU2", "WAV", "NOI" };
    return names[size_t(ch & 3)];
}

} // namespace

void ChipBoyProcessor::editBank(const String& name, const std::function<void(bank::Bank&)>& fn)
{
    auto before = bankShared_;
    const String nameBefore = bankName();
    const int tab = active().id;
    mutateBank(fn);
    if (bankShared_ == before) return;
    history_.perform(std::make_unique<BankAction>(*this, tab, before, nameBefore, bankShared_, bankName()), name);
}

void ChipBoyProcessor::editSong(const String& name, const std::function<void(tracker::Song&)>& fn)
{
    auto before = songShared_;
    const int tab = active().id;
    mutateSong(fn);
    if (songShared_ == before) return;
    history_.perform(std::make_unique<SongAction>(*this, tab, before, songShared_), name);
}

void ChipBoyProcessor::loadBankEdit(const String& name, const bank::Bank& b, const String& newName)
{
    auto before = bankShared_;
    const String nameBefore = bankName();
    const int tab = active().id;
    publishBank(std::shared_ptr<const bank::Bank>(new bank::Bank(b)));
    active().bankName = newName;
    history_.perform(std::make_unique<BankAction>(*this, tab, before, nameBefore, bankShared_, newName), name);
}

void ChipBoyProcessor::setBankNameEdit(const String& n)
{
    if (n == bankName()) return;
    auto snapshot = bankShared_;
    const String nameBefore = bankName();
    const int tab = active().id;
    active().bankName = n;
    active().dirty = true;
    history_.perform(std::make_unique<BankAction>(*this, tab, snapshot, nameBefore, snapshot, n), "Bank name " + nameBefore + " " + String(CharPointer_UTF8("\xe2\x86\x92")) + " " + n);
}

/// The song's master tempo and the Song tempo parameter move together, as one
/// undo step (section 19). Everything else about a song edit is editSong's.
void ChipBoyProcessor::setMasterTempo(double bpm)
{
    const double v = std::clamp(bpm, 40.0, 255.0);
    const double was = songShared_ ? songShared_->tempoBpm : 120.0;
    if (std::fabs(was - v) < 1e-9) return;
    auto before = songShared_;
    const int tab = active().id;
    auto copy = std::make_shared<tracker::Song>();          // 300 KB: never on the stack
    if (songShared_) *copy = *songShared_;
    copy->tempoBpm = v;
    publishSong(std::move(copy), /*fromFile*/ true);        // the song's tempo becomes the parameter's
    history_.perform(std::make_unique<SongAction>(*this, tab, before, songShared_),
                     "Tempo " + String(int(std::lround(was))) + " " + String(CharPointer_UTF8("\xe2\x86\x92")) + " " + String(int(std::lround(v))));
}

void ChipBoyProcessor::restoreBank(int tabId, std::shared_ptr<const bank::Bank> b, const String& newName)
{
    if (!b) return;
    const int i = tabIndexOfId(tabId);
    if (i < 0) return;                        // its tab has been closed
    setActiveTab(i);
    publishBank(std::move(b));
    active().bankName = newName;
}

void ChipBoyProcessor::restoreSong(int tabId, std::shared_ptr<const tracker::Song> s)
{
    if (!s) return;
    const int i = tabIndexOfId(tabId);
    if (i < 0) return;
    setActiveTab(i);
    auto copy = std::make_shared<tracker::Song>();          // on the heap, as mutateSong's is
    *copy = *s;
    // The snapshot carries the master tempo it was taken with, so undoing a
    // tempo edit moves the parameter back with it (section 19).
    publishSong(std::move(copy), /*fromFile*/ true);
}

void ChipBoyProcessor::setChannelArm(int ch, bool on)
{
    const int c = ch & 3;
    editSong(String(channelShortName(c)) + (on ? " armed" : " unarmed"),
             [c, on](tracker::Song& s) { s.recordArm[size_t(c)] = on; });
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
    clock_.prepare(sampleRate);
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

bool ChipBoyProcessor::keyswitchNote(int ch, const bank::Bank* bank, uint8_t note) const
{
    const auto& cp = channelParams[size_t(ch & 3)];
    // On a Hybrid channel the octave is inert whatever the parameter says
    // (section 20): the note did nothing, so it is not a cell either.
    const auto* s = songPtr_.load(std::memory_order_acquire);
    const bool hybrid = s && s->noteSource[size_t(ch & 3)] == tracker::NoteSource::Hybrid;
    if (!hybrid && paramInt(cp.keyswitch) == 0) return false;
    const bank::Instrument* i = bank && !hybrid ? bank->instrument(paramInt(cp.instrument)) : nullptr;
    const auto type = i ? i->type : (ch == 2 ? bank::InstrumentType::Wave : ch == 3 ? bank::InstrumentType::Noise : bank::InstrumentType::Pulse);
    const int base = type == bank::InstrumentType::Pulse ? 24 : 12;   // the octave below the playable floor
    return note >= base && note < base + 12;
}

void ChipBoyProcessor::flushChannel(int ch, std::vector<driver::NoteEvent>& dst, uint32_t offset)
{
    driver::NoteEvent e;
    e.kind = driver::NoteEvent::AllNotesOff;
    e.channel = uint8_t(ch & 3);
    e.offset = offset;
    dst.push_back(e);
}

/// Recording (docs/COMMANDS_AND_TEMPO.md section 9.4): the Player decides what
/// a cell holds; this puts it on the queue for the message thread.
///
/// The slots a step records are the channel's parameters, not the driver's
/// running slots: a parameter holds still across a block, so its value at a
/// step's tick is this block's value -- the one the driver applies at that
/// tick. The driver's own slots would be a block stale here, since the
/// recorder runs before the driver has played the block.
void ChipBoyProcessor::recordNote(const driver::NoteEvent& e, double tickAtEvent, const bank::Bank* bank)
{
    const int ch = e.channel & 3;
    if (keyswitchNote(ch, bank, e.a)) return;      // it selects an instrument, it is not a cell
    const bool off = e.kind == driver::NoteEvent::NoteOff || (e.kind == driver::NoteEvent::NoteOn && e.b == 0);
    // What the channel really read: a Hybrid channel's slots are inert, so
    // its cells carry no commands from them (section 20).
    const auto p = driver_.effective(ch);
    // The command octave (section 13): a note below C0 never sounds, it fires
    // the channel's slots. It records as a slot-only cell at its step, which
    // replays the same way; its note-off means nothing. It is inert on a
    // Hybrid channel, so nothing is written there.
    if (e.a < 12) {
        if (off || driver_.hybrid(ch)) return;
        tracker::RecordMessage cmdCell;
        if (player_.recordSlots(ch, tickAtEvent, p.cmd[0], p.cmd[1], cmdCell, /*force*/ true)) recordFifo_.push(cmdCell);
        return;
    }
    tracker::RecordMessage m;
    // The driver stamped this event with what the note did: the instrument it
    // loaded and whether it was plain (section 9.4).
    // VEL is written only when the velocity set the volume: under the bank or
    // ignored modes it did not, and a blank VEL replays the instrument's volume
    // in any instance, whatever that instance's Velocity mode (section 9.1).
    const uint8_t velCol = p.velocityMode == 0 ? e.b : uint8_t(0);
    if (player_.recordNote(ch, tickAtEvent, e.a, velCol, off, e.plain, e.loaded, p.table, p.cmd[0], p.cmd[1], m))
        recordFifo_.push(m);
}

void ChipBoyProcessor::recordSlots(int ch, double tick)
{
    const auto p = driver_.effective(ch);
    tracker::RecordMessage m;
    if (player_.recordSlots(ch, tick, p.cmd[0], p.cmd[1], m)) recordFifo_.push(m);
}

void ChipBoyProcessor::applyRecordMessages()
{
    tracker::RecordMessage m;
    std::shared_ptr<tracker::Song> copy;
    while (recordFifo_.pop(m)) {
        if (!copy) { copy = std::make_shared<tracker::Song>(); if (songShared_) *copy = *songShared_; }
        auto& chain = copy->chain[size_t(m.channel & 3)];
        if (chain.size() <= m.row) chain.resize(size_t(m.row) + 1, 0);
        uint8_t slot = chain[m.row];
        if (slot == 0) {
            for (int i = 0; i < tracker::kPhraseSlots; ++i) if (!copy->phrases[size_t(i)].used) { slot = uint8_t(i + 1); break; }
            if (slot == 0) continue;                    // the song is full
            copy->phrases[size_t(slot - 1)].used = true;
            chain[m.row] = slot;
        }
        auto& cell = copy->phrases[size_t(slot - 1)].cells[size_t(m.step) % size_t(tracker::kMaxSteps)];
        // An OFF never displaces a note-on: the note there ends the last one
        // anyway (section 9.4). A command column is written only when the
        // message carries a letter, so a note and a slot change at the same
        // step merge whichever order they arrive in.
        if (!m.slotsOnly) {
            if (m.cell.note == tracker::kNoteOff) { if (cell.note == 0) cell.note = tracker::kNoteOff; }
            else { cell.note = m.cell.note; cell.vel = m.cell.vel; cell.inst = m.cell.inst; cell.table = m.cell.table; }
        }
        if (m.cell.cmd1.cmd != bank::Cmd::None) cell.cmd1 = m.cell.cmd1;
        if (m.cell.cmd2.cmd != bank::Cmd::None) cell.cmd2 = m.cell.cmd2;
    }
    if (copy) publishSong(std::move(copy));
}

/* ----------------------------------------------------------- song files */

bool ChipBoyProcessor::loadSongFile(const File& file, SongReport& report)
{
    auto s = std::make_shared<tracker::Song>();          // 300 KB: never on the stack
    auto b = std::make_shared<bank::Bank>();             // and 41 KB
    if (!plugin::loadSong(file, *s, report, bankShared_.get(), b.get())) return false;
    auto before = songShared_;
    auto bankBefore = bankShared_;
    const String nameBefore = bankName();
    const int tab = active().id;
    // Format 5 brings the sounds with it (section 18): the tab's bank is the
    // file's, and the song and the bank are one undo step.
    if (report.hasBank) {
        publishBank(std::shared_ptr<const bank::Bank>(std::move(b)));
        active().bankName = report.bankName.isNotEmpty() ? report.bankName : nameBefore;
    }
    publishSong(std::move(s), /*fromFile*/ true);
    const String what = "Load song " + file.getFileNameWithoutExtension();
    if (report.hasBank)
        history_.perform(std::make_unique<BankAction>(*this, tab, bankBefore, nameBefore, bankShared_, bankName()), what);
    history_.perform(std::make_unique<SongAction>(*this, tab, before, songShared_), what);
    active().file = file;
    active().name = file.getFileNameWithoutExtension();
    active().dirty = false;
    return true;
}

bool ChipBoyProcessor::openSongFileInTab(const File& file, SongReport& report)
{
    auto s = std::make_shared<tracker::Song>();          // 300 KB: never on the stack
    auto b = std::make_shared<bank::Bank>();
    if (!plugin::loadSong(file, *s, report, bankShared_.get(), b.get())) return false;
    // Format 5 carries its own sounds; an older file takes a copy of the
    // bank the window is on, and the report says where it differs (18).
    std::shared_ptr<const bank::Bank> bank;
    String bankNameForTab = report.bankName;
    if (report.hasBank) bank = std::shared_ptr<const bank::Bank>(std::move(b));
    else {
        auto copy = std::make_shared<bank::Bank>();
        if (bankShared_) *copy = *bankShared_;
        bank = std::shared_ptr<const bank::Bank>(std::move(copy));
        bankNameForTab = bankName();
    }
    const int index = addTab(std::shared_ptr<const tracker::Song>(std::move(s)), std::move(bank),
                             file.getFileNameWithoutExtension(),
                             bankNameForTab.isNotEmpty() ? bankNameForTab : String("Factory"));
    tabs_[size_t(index)].file = file;
    return true;
}

bool ChipBoyProcessor::saveSongFile(const File& file)
{
    if (!songShared_ || !bankShared_) return false;
    // The song carries the tempo it was played at -- the Song tempo
    // parameter, as the plugin state does (sections 4 and 19).
    const auto out = std::make_unique<tracker::Song>();
    *out = *songShared_;
    out->tempoBpm = songTempoParam();
    if (!plugin::saveSong(*out, *bankShared_, file, bankName())) return false;
    active().file = file;
    active().name = file.getFileNameWithoutExtension();
    active().dirty = false;
    return true;
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
    // The Song tempo parameter is the active song's master tempo (section 19):
    // a host moving the lane writes it back into the song, in memory and with
    // no undo step. It is also the base a T cell modifies and a T reverting
    // goes back to, so the tempo map is rebuilt on it here (section 4).
    if (songShared_ && (std::fabs(songTempoParam() - songShared_->tempoBpm) > 1e-9
                        || (!songShared_->tempoMap.empty() && std::fabs(songTempoParam() - tempoBase_) > 1e-9)))
        mutateSong([](tracker::Song&) {});
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
        const uint64_t st2 = packState2(driver_.view(ch));
        scopes_.state[size_t(ch)].store(st, std::memory_order_release);
        scopes_.state2[size_t(ch)].store(st2, std::memory_order_release);
        scopes_.tableRun[size_t(ch)].store(packTableRun(driver_.view(ch)), std::memory_order_release);
        if (owned & (1u << ch)) { r->slots[ch].state.store(st, std::memory_order_release); r->slots[ch].state2.store(st2, std::memory_order_release); }
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
    const auto* bankNow = bankPtr_.load(std::memory_order_acquire);   // "bank" would shadow the namespace
    driver_.setBank(bankNow);
    driver_.setSong(song);
    player_.setSong(song);
    applyModel();
    applyHardwareOptions();

    driver::GlobalParams g;
    g.masterL = uint8_t(std::clamp(paramInt(pMasterL_, 7), 0, 7));
    g.masterR = uint8_t(std::clamp(paramInt(pMasterR_, 7), 0, 7));
    driver_.setGlobal(g);
    driver_.setNotesOnTick(paramInt(pNotesOnTick_) != 0);
    {
        const uint32_t solo = soloMask_.load(), mute = muteMask_.load();
        driver_.setGateMask((solo ? solo : 15u) & ~mute & 15u);
    }

    // --- transport ------------------------------------------------------
    driver::Transport t;
    uint64_t hostFrame = kNoHostFrame;
    if (auto* ph = getPlayHead()) {
        if (const auto pos = ph->getPosition()) {
            t.playing = pos->getIsPlaying();
            const auto bpm = pos->getBpm(); const auto ppq = pos->getPpqPosition();
            t.valid = bpm.hasValue() && ppq.hasValue();
            t.bpm = bpm.orFallback(120.0); t.ppq = ppq.orFallback(0.0);
            if (const auto secs = pos->getTimeInSeconds()) { t.seconds = *secs; t.timeValid = true; }
            if (const auto tis = pos->getTimeInSamples()) if (*tis >= 0) hostFrame = uint64_t(*tis);
        }
    }
    // A host that gives a beat position but no time in seconds: the song's own
    // timeline needs a time base, and at the host's tempo the two agree. It
    // re-anchors on a jump, so a host tempo change costs nothing (section 4).
    if (t.valid && !t.timeValid && t.bpm > 1.0) { t.seconds = t.ppq * 60.0 / t.bpm; t.timeValid = true; }
    // No play head, or one that offers no position: the plugin runs the song
    // itself, on its own clock at the Song tempo (section 16). The Standalone
    // has no play head at all, so this is what makes it play.
    clock_.setOwnsTransport(!t.valid);
    ownsTransport_.store(clock_.ownsTransport());
    const bool link = linkActive_;
    const int behind = prevBlock_ > 0 ? prevBlock_ : n;
    if (link && t.valid) {
        // One block behind (11.4), in both of the transport's units.
        t.ppq -= behind * (t.bpm / 60.0 / sampleRate_);
        t.seconds -= behind / sampleRate_;
    }

    // --- the clock: where the ticks are, and where the tracker is --------
    driver::ClockConfig cc;
    // While the plugin owns the transport the tempo is the song's: there is no
    // host beat to follow (section 16).
    cc.source = paramInt(pTempoSource_) != 0 || clock_.ownsTransport() ? driver::TempoSource::Song : driver::TempoSource::Host;
    cc.songTempo = songTempoParam();
    cc.songStartSeconds = song ? song->songStartSeconds : 0.0;
    // A T slot in force is the song's tempo from now on; the lowest channel
    // holding one wins, as two lanes cannot both be the timeline.
    for (int ch = 0; ch < 4; ++ch) {
        bool found = false;
        for (int i = 0; i < 2; ++i) {
            const auto& c = driver_.slot(ch, i);
            if (c.cmd == bank::Cmd::T && !bank::isRevert(c)) { cc.songTempo = double(std::clamp<int>(c.a, 40, 255)); found = true; break; }
        }
        if (found) break;
    }
    clock_.setConfig(cc);
    if (song && !song->tempoMap.empty()) clock_.setTempoMap(song->tempoMap.data(), song->tempoMap.size());
    else clock_.setTempoMap(nullptr, 0);
    // The loop, in the rows of the longest chain, handed over as ticks: the
    // clock knows ticks, the song knows where its rows are. The whole song is
    // the longest channel, which is what the own transport loops (section 25).
    {
        const int ch = song ? tracker::longestChain(*song) : 0;
        const int rows = song ? std::max(1, song->rows(ch)) : 1;
        const int from = std::clamp(loopFrom_.load(), 0, rows - 1);
        const int to = loopTo_.load() < 0 ? rows : std::clamp(loopTo_.load(), from + 1, rows);
        if (song) clock_.setLoop(loopOn_.load(), tracker::rowStartTick(*song, ch, from),
                                 loopTo_.load() < 0 ? tracker::songTicks(*song) : tracker::rowStartTick(*song, ch, to));
        else clock_.setLoop(false, 0, 0);
    }
    if (const int req = transportRequest_.exchange(0)) { if (req == 1) clock_.ownPlay(); else clock_.ownStop(); }
    clock_.process(t, uint32_t(n), frames_);
    const bool songSource = cc.source == driver::TempoSource::Song;
    // Whoever owns the transport, this is whether it is running.
    const bool playing = clock_.playing();
    playing_.store(playing); ppq_.store(t.ppq); bpm_.store(t.bpm);
    songTempo_.store(songSource); tempo_.store(clock_.bpm());
    // The position the window shows follows the transport (section 9.1). The
    // clock free-runs while it is stopped so that tables and vibrato stay
    // alive; that tick is not where the tracker is, so it is not published.
    {
        int64_t at = trackerTick_.load();
        if (playing) at = clock_.tickAtBlockStart();
        else if (clock_.ownsTransport()) { }                          // stopped: it stands where it stopped
        else if (t.valid && !songSource) at = int64_t(std::floor(t.ppq * driver::kTicksPerBeat));
        else if (t.valid && t.timeValid) at = int64_t(std::floor(clock_.ticksAtSeconds(t.seconds)));
        trackerTick_.store(std::max<int64_t>(0, at));
    }
    // Where each channel is in its own chain, for the window (section 25).
    for (int ch = 0; ch < 4; ++ch) {
        channelRow_[size_t(ch)].store(player_.position(ch).row);
        channelStep_[size_t(ch)].store(player_.position(ch).step);
    }

    // --- events ---------------------------------------------------------
    events_.clear();
    if (link) {
        // This block's MIDI waits a block so it lines up with the Voices.
        events_.swap(delayed_);
        delayed_.clear();
        for (auto& e : events_) e.offset = std::min<uint32_t>(e.offset, uint32_t(n - 1));
        for (const auto meta : midi) routeMidi(meta.getMessage(), meta.samplePosition, delayed_);
    } else {
        if (prevLink_) {
            // Link mode off: what the Voices were driving is flushed, and the
            // block of MIDI held for them is played rather than dropped.
            for (int ch = 0; ch < 4; ++ch) flushChannel(ch, events_);
            for (auto& e : delayed_) { e.offset = std::min<uint32_t>(e.offset, uint32_t(n - 1)); events_.push_back(e); }
        }
        delayed_.clear();
        for (const auto meta : midi) routeMidi(meta.getMessage(), meta.samplePosition, events_);
    }
    prevLink_ = link;

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

    // --- a lane changing hands (section 9.1) -----------------------------
    // A channel whose feed changes -- its Source, its Trk/Roll choice, or a
    // Voice letting it go -- must not be left ringing. The flushes go in front
    // of this block's events, so a note arriving now still sounds.
    {
        size_t flushed = 0;
        // A tab switch, from the message thread: a different piece of music,
        // so nothing of the old one is left ringing (section 18).
        const uint32_t asked = flushRequest_.exchange(0);
        for (int ch = 0; ch < 4; ++ch) {
            if (asked & (1u << ch)) { flushChannel(ch, events_); ++flushed; }
            const int src = paramInt(channelParams[size_t(ch)].source);
            if (prevSource_[size_t(ch)] >= 0 && src != prevSource_[size_t(ch)]) { flushChannel(ch, events_); ++flushed; }
            prevSource_[size_t(ch)] = src;
            const int ns = song ? int(song->noteSource[size_t(ch)]) : -1;
            if (ns >= 0 && prevNoteSource_[size_t(ch)] >= 0 && ns != prevNoteSource_[size_t(ch)]) { flushChannel(ch, events_); ++flushed; }
            prevNoteSource_[size_t(ch)] = ns;
            if ((prevOwned_ & ~owned) & (1u << ch)) { flushChannel(ch, events_); ++flushed; }
        }
        prevOwned_ = owned;
        if (flushed) std::rotate(events_.begin(), events_.end() - long(flushed), events_.end());
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
        // A G slot is the channel's groove; the Player owns the timing and
        // works out at every tick which groove that leaves in force (9.2).
        uint8_t groove = tracker::kGrooveNone;
        for (int i = 0; i < 2; ++i) if (driver_.params(ch).cmd[i].cmd == bank::Cmd::G && !bank::isRevert(driver_.params(ch).cmd[i])) groove = uint8_t(std::clamp<int>(driver_.params(ch).cmd[i].a, 0, 16));
        player_.setGrooveSlot(ch, groove);
        driver_.setViewGroove(ch, player_.groove(ch));
        // A G inside a table sets that run's row lengths from the song's
        // groove (9.2): the driver remembers the slot the table asked for and
        // is handed its tick counts here, straight (null) for slot 0.
        {
            const int ts = driver_.tableGrooveSlot(ch);
            driver_.setTableGroove(ch, song && ts >= 1 && ts <= 16 ? song->grooves[size_t(ts - 1)].ticks.data() : nullptr);
        }
    }

    // --- the tracker ----------------------------------------------------
    uint32_t trackerMask = 0, armMask = 0;
    if (song) for (int ch = 0; ch < 4; ++ch) {
        if (song->noteSource[size_t(ch)] == tracker::NoteSource::Tracker) trackerMask |= 1u << ch;
        if (song->recordArm[size_t(ch)]) armMask |= 1u << ch;
    }
    // An armed channel records whatever it plays from; an unarmed one never
    // does (section 14). An armed tracker channel lets the MIDI through and
    // its lane goes quiet meanwhile; an unarmed one plays its cells.
    const bool rec = recordArm_.load() && playing;
    const uint32_t recMask = rec ? armMask : 0u;
    // A channel that stops recording drops what was played through onto it,
    // before the lane it was borrowing plays its own cells again (9.1).
    for (int ch = 0; ch < 4; ++ch)
        if ((prevRecMask_ & ~recMask & trackerMask) & (1u << ch)) flushChannel(ch, events_);
    prevRecMask_ = recMask;
    player_.setMuteMask(recMask & trackerMask);
    driver_.setRecordMask(recMask);
    player_.process(clock_.ticks(), clock_.tickCount(), playing, events_);
    if (rec) {
        // Ticks are the recorder's ruler too: a note lands on the step nearest
        // the tick it arrived on, and the slots are read at each step's own
        // tick, on the channel's own grid (section 9.4).
        if (!recWasArmed_) player_.resetRecord();
        const auto* tk = clock_.ticks();
        for (size_t k = 0; k < clock_.tickCount(); ++k)
            for (int ch = 0; ch < 4; ++ch) {
                int bar = 0, step = 0;
                if ((recMask & (1u << ch)) && player_.stepAt(ch, tk[k].tick, bar, step)) recordSlots(ch, double(tk[k].tick));
            }
    }
    recWasArmed_ = rec;

    // At one sample the order is: a flush, then the song's cells, then the
    // MIDI's offs, then its ons. A Hybrid channel's cell chooses the
    // instrument and holds the commands a note-on in the same tick takes
    // (section 20), so it has to be in front of that note; a flush is in
    // front of everything; and a note-off goes before a note-on so a host
    // that ends one note and starts the next on the same sample in the other
    // order -- FL Studio at a loop point -- does not have the new note cut by
    // the old one's off (section 43).
    {
        auto rank = [](const driver::NoteEvent& e) {
            if (e.kind == driver::NoteEvent::AllNotesOff) return 0;
            if (e.source == driver::NoteEvent::Tracker) return 1;
            const bool off = e.kind == driver::NoteEvent::NoteOff || (e.kind == driver::NoteEvent::NoteOn && e.b == 0);
            return off ? 2 : 3;
        };
        std::stable_sort(events_.begin(), events_.end(), [&rank](const driver::NoteEvent& a, const driver::NoteEvent& b) {
            return a.offset != b.offset ? a.offset < b.offset : rank(a) < rank(b);
        });
    }
    for (const auto& e : events_) {
        if (e.kind == driver::NoteEvent::NoteOn && e.b) lastNotes[size_t(e.channel & 3)].store(e.a);
        else if (e.kind == driver::NoteEvent::NoteOff || (e.kind == driver::NoteEvent::NoteOn && !e.b)) { const int cur = lastNotes[size_t(e.channel & 3)].load(); if (cur == e.a) lastNotes[size_t(e.channel & 3)].store(-1); }
    }

    // --- drive the chip -------------------------------------------------
    writes_.clear();
    driver_.process(events_.data(), events_.size(), uint32_t(n), frames_, clock_.ticks(), clock_.tickCount(), cycleAt_, writes_);
    // The notes are recorded now that the driver has played them: the cell's
    // instrument column is the instrument the note actually loaded, and
    // whether it was plain, which only the note-on itself decides (9.4). The
    // driver stamped both on each event as it played it, so two notes in one
    // block are recorded as the two things they were.
    if (rec) {
        const double tickPerFrame = clock_.bpm() * driver::kTicksPerBeat / 60.0 / sampleRate_;
        const double tick0 = double(clock_.tickAtBlockStart());
        for (const auto& e : events_)
            if (e.source == driver::NoteEvent::Midi && (recMask & (1u << (e.channel & 3))) && (e.kind == driver::NoteEvent::NoteOn || e.kind == driver::NoteEvent::NoteOff))
                recordNote(e, tick0 + e.offset * tickPerFrame, bankNow);
    }
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
    // Writes arrive in cycle order, and every one of them is a write a Game
    // Boy driver could make: nothing waits for the pulse output's low half any
    // more, because a program on the hardware cannot wait for it (section 26).
    for (const auto& w : writes_) {
        apu_.runTo(std::max(w.cycle, apu_.cycle()));
        apu_.write(w.addr, w.value);
    }
}

/* ------------------------------------------------------------- state */

AudioProcessorEditor* ChipBoyProcessor::createEditor()
{
    return new ChipBoyEditor(*this);
}

void ChipBoyProcessor::getStateInformation(MemoryBlock& dest)
{
    ValueTree root("ChipBoyState");
    // Version 2 holds every tab; version 1 held one song and one bank, and
    // still reads as a single tab (section 18).
    root.setProperty("version", 2, nullptr);
    root.setProperty("uuid", uuid_, nullptr);
    root.setProperty("name", instanceName_, nullptr);
    root.setProperty("activeTab", activeTab_, nullptr);
    root.addChild(apvts.copyState(), -1, nullptr);
    ValueTree tabs("tabs");
    for (int i = 0; i < tabCount(); ++i) {
        const SongTab& tab = tabs_[size_t(i)];
        ValueTree t("tab");
        t.setProperty("name", tab.name, nullptr);
        t.setProperty("file", tab.file == File() ? String() : tab.file.getFullPathName(), nullptr);
        t.setProperty("bankName", tab.bankName, nullptr);
        if (tab.bank) t.setProperty("bank", bankToJson(*tab.bank), nullptr);
        if (tab.song) {
            // The active song carries the tempo it was played at: the Song
            // tempo parameter, which may have moved since it was published
            // (docs/COMMANDS_AND_TEMPO.md sections 4 and 19). The others
            // carry their own master tempo.
            const auto saved = std::make_unique<tracker::Song>();   // 83 KB: not on the message thread's stack
            *saved = *tab.song;
            if (i == activeTab_) saved->tempoBpm = songTempoParam();
            t.setProperty("song", songToJson(*saved), nullptr);
        }
        tabs.addChild(t, -1, nullptr);
    }
    root.addChild(tabs, -1, nullptr);
    MemoryOutputStream mo(dest, false);
    root.writeToStream(mo);
}

void ChipBoyProcessor::setStateInformation(const void* data, int size)
{
    const ValueTree root = ValueTree::readFromData(data, size_t(size));
    if (!root.isValid() || !root.hasType("ChipBoyState")) return;
    // A state restore is the host loading a project, not an edit: what came
    // before it is gone (UI_DESIGN section 2.1).
    history_.clear();
    if (root.hasProperty("uuid") && root["uuid"].toString().isNotEmpty()) {
        const String u = root["uuid"].toString();
        if (u != uuid_) { uuid_ = u; if (linkHost_.published()) linkHost_.unpublish(); }   // the timer republishes under the saved UUID
    }
    if (root.hasProperty("name")) instanceName_ = root["name"].toString();
    const ValueTree params = root.getChildWithName(apvts.state.getType());
    if (params.isValid()) apvts.replaceState(params);

    // One tab per saved song (section 18). A state written before tabs holds
    // one bank and one song at the root, which is exactly one tab. The saved
    // tabs are added after the ones that are open and those are dropped at
    // the end, so the list is never empty and never publishes a null.
    const ValueTree tabs = root.getChildWithName("tabs");
    const int had = tabCount();
    const int n = tabs.isValid() ? tabs.getNumChildren() : 0;
    for (int i = 0; i < n; ++i) {
        const ValueTree t = tabs.getChild(i);
        auto b = std::make_shared<bank::Bank>();
        // `new T(prvalue)` builds it in its heap block: a factory Bank is
        // 41 KB and must never be a stack temporary.
        if (!t.hasProperty("bank") || !bankFromJson(t["bank"].toString(), *b))
            b = std::shared_ptr<bank::Bank>(new bank::Bank(bank::Bank::factory()));
        auto s = std::make_shared<tracker::Song>();
        if (t.hasProperty("song")) songFromJson(t["song"].toString(), *s);
        const int index = addTab(std::shared_ptr<const tracker::Song>(std::move(s)),
                                 std::shared_ptr<const bank::Bank>(std::move(b)),
                                 t["name"].toString(), t["bankName"].toString());
        const String path = t["file"].toString();
        if (path.isNotEmpty()) tabs_[size_t(index)].file = File(path);
    }
    if (tabCount() == had) {
        // Before version 2, or a state with no tabs at all.
        auto b = std::make_shared<bank::Bank>();
        if (!root.hasProperty("bank") || !bankFromJson(root["bank"].toString(), *b))
            b = std::shared_ptr<bank::Bank>(new bank::Bank(bank::Bank::factory()));
        auto s = std::make_shared<tracker::Song>();
        if (root.hasProperty("song")) songFromJson(root["song"].toString(), *s);
        addTab(std::shared_ptr<const tracker::Song>(std::move(s)),
               std::shared_ptr<const bank::Bank>(std::move(b)), "Song",
               root.hasProperty("bankName") ? root["bankName"].toString() : String("Factory"));
    }
    tabs_.erase(tabs_.begin(), tabs_.begin() + had);          // the tabs this state replaces
    activeTab_ = std::clamp(activeTab_ - had, 0, tabCount() - 1);
    // addTab left the last one active; the saved one takes over.
    setActiveTab(std::clamp(root.hasProperty("activeTab") ? int(root["activeTab"]) : 0, 0, tabCount() - 1));
    // The parameter follows the active tab, whatever the tabs did to it.
    tempoBase_ = active().song ? active().song->tempoBpm : 120.0;
    setSongTempoParam(tempoBase_);
    activate(active());
    for (auto& tab : tabs_) tab.dirty = false;
}

} // namespace chipboy::plugin

#ifndef CHIPBOY_NO_PLUGIN_FILTER
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new chipboy::plugin::ChipBoyProcessor();
}
#endif
