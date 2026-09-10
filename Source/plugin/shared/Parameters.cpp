#include "plugin/shared/Parameters.h"

namespace chipboy::plugin {

using namespace juce;

namespace {

std::unique_ptr<AudioParameterInt> intParam(const String& id, const String& name, int lo, int hi, int def,
                                            std::function<String(int, int)> toText = {})
{
    auto attrs = AudioParameterIntAttributes().withStringFromValueFunction(toText ? toText : [](int v, int) { return String(v); });
    return std::make_unique<AudioParameterInt>(ParameterID(id, 1), name, lo, hi, def, attrs);
}
std::unique_ptr<AudioParameterChoice> choiceParam(const String& id, const String& name, const StringArray& c, int def)
{
    return std::make_unique<AudioParameterChoice>(ParameterID(id, 1), name, c, def);
}
std::unique_ptr<AudioParameterBool> boolParam(const String& id, const String& name, bool def)
{
    return std::make_unique<AudioParameterBool>(ParameterID(id, 1), name, def);
}

String instText(int v, int top) { return v == top ? "inst" : String(v); }

} // namespace

namespace {
/// The lane's letters, in the enum's order minus H (tables only).
/// `B` is last so every other letter keeps the choice index it has always had
/// and a saved automation value still means what it did (section 73).
constexpr bank::Cmd kLaneCmds[] = {
    bank::Cmd::A, bank::Cmd::C, bank::Cmd::D, bank::Cmd::E, bank::Cmd::F, bank::Cmd::G,
    bank::Cmd::K, bank::Cmd::L, bank::Cmd::M, bank::Cmd::O, bank::Cmd::P, bank::Cmd::R,
    bank::Cmd::S, bank::Cmd::T, bank::Cmd::V, bank::Cmd::W, bank::Cmd::Z, bank::Cmd::B,
};
}

StringArray commandChoices()
{
    StringArray c { "none" };
    for (auto cmd : kLaneCmds) c.add(bank::cmdLetter(cmd));
    return c;
}

bank::Cmd cmdFromChoice(int index)
{
    const int i = index - 1;
    return i >= 0 && i < int(std::size(kLaneCmds)) ? kLaneCmds[size_t(i)] : bank::Cmd::None;
}

int choiceFromCmd(bank::Cmd c)
{
    for (int i = 0; i < int(std::size(kLaneCmds)); ++i) if (kLaneCmds[size_t(i)] == c) return i + 1;
    return 0;
}

namespace {
/// V's depth, as the semitones LSDj's table means (section 7).
String vibDepthText(int y)
{
    static const char* n[16] = { "1/8", "1/4", "3/8", "1/2", "3/4", "1", "1 1/2", "2",
                                 "2 1/2", "3", "3 1/2", "4", "5", "6", "7", "8" };
    return String(n[std::clamp(y, 0, 15)]) + " st";
}
/// One side of M: absolute, unchanged, or a relative step (section 7).
String masterSideText(int v)
{
    const int n = std::clamp(v, 0, 15);
    if (n < 8) return String(n);
    if (n >= 9 && n <= 11) return "+" + String(n - 8);
    if (n >= 13) return String(CharPointer_UTF8("\xe2\x88\x92")) + String(n - 12);
    return String(CharPointer_UTF8("\xe2\x80\x94"));      // 8 and 12 leave the side alone
}
/// R's volume step: 0 none, 1-7 up by that much, 9-15 down by x - 8.
String retrigStepText(int x)
{
    const int n = std::clamp(x, 0, 15);
    if (n == 0 || n == 8) return {};
    return n < 8 ? "vol +" + String(n) : "vol " + String(CharPointer_UTF8("\xe2\x88\x92")) + String(n - 8);
}
/// A byte read as two's complement: 0xFE is -2 (section 34).
int signedByte(int v) { const int b = v & 255; return b >= 128 ? b - 256 : b; }
/// What a letter's revert form puts back (docs/COMMANDS_AND_TEMPO.md 3).
String commandRevertText(bank::Cmd c)
{
    switch (c) {
        case bank::Cmd::A: return "stop the table";
        case bank::Cmd::G: return "the phrase's groove";
        case bank::Cmd::M: return "the master parameters";
        case bank::Cmd::P: return "no offset, no bend";
        case bank::Cmd::T: return "the song tempo";
        case bank::Cmd::E: case bank::Cmd::F: case bank::Cmd::O:
        case bank::Cmd::S: case bank::Cmd::V: case bank::Cmd::W:
            return "the instrument's own";
        case bank::Cmd::None: case bank::Cmd::C: case bank::Cmd::D: case bank::Cmd::H:
        case bank::Cmd::K: case bank::Cmd::L: case bank::Cmd::R: case bank::Cmd::Z:
            break;                     // per-note letters leave nothing to put back
    }
    return {};
}
} // namespace

String commandArgText(const bank::Command& c)
{
    if (bank::isRevert(c) && bank::cmdPersists(c.cmd)) return commandRevertText(c.cmd);
    const int x = c.a, y = c.b;
    const String dot = String(CharPointer_UTF8(" \xc2\xb7 "));
    switch (c.cmd) {
        case bank::Cmd::None: return {};
        case bank::Cmd::A:    return x == 0 ? String("stop") : "table " + String(x);
        case bank::Cmd::C:    return y == 0 ? (x == 0 ? String("off") : "0" + dot + "+" + String(x))
                                            : "0" + dot + "+" + String(x) + dot + "+" + String(y);
        case bank::Cmd::D:    return String(x) + " ticks";
        case bank::Cmd::E:    return "vol " + String(x) + dot + ((y & 7) == 0 ? String("hold") : ((y & 8) ? "up " : "down ") + String(y & 7));
        case bank::Cmd::F:    return "frame " + String(x);
        case bank::Cmd::G:    return x == 0 ? String("straight") : "groove " + String(x);
        case bank::Cmd::H:    return (x == 0 ? String("forever") : String(x) + (x == 1 ? " time" : " times")) + dot + "row " + String(y);
        case bank::Cmd::K:    return "after " + String(x);
        case bank::Cmd::L:    return x == 0 ? String("instant") : "over " + String(x);
        case bank::Cmd::M:    return "L " + masterSideText(x) + dot + "R " + masterSideText(y);
        case bank::Cmd::O:    { static const char* n[] = { "off", "L", "R", "both" }; return n[x & 3]; }
        case bank::Cmd::P:    { const int v = signedByte(x); return v == 0 ? String("hold") : (v > 0 ? "+" : "") + String(v); }
        case bank::Cmd::R:    { const String v = retrigStepText(x); return (y == 0 ? String("once") : "every " + String(y)) + (v.isEmpty() ? String() : dot + v); }
        case bank::Cmd::S:    return "rate " + String(x & 7) + dot + "shift " + String(y & 7) + ((y & 8) ? String(" down") : String(" up"));
        case bank::Cmd::T:    return String(x) + " BPM";   // 40-295, the byte wraps (section 34)
        case bank::Cmd::V:    return x == 0 ? String("off") : "speed " + String(x) + dot + vibDepthText(y);
        case bank::Cmd::W:    return "duty/wave " + String(x);
        case bank::Cmd::Z:    return "add 0-" + String(x) + (y ? dot + "0-" + String(y) : String());
        // Section 73: in a cell the two nibbles are independent x/15 rolls and the
        // note sounds if either passes; in a table it is a hop to row y taken
        // x/16 of the time. The readout gives the cell's odds, which is where
        // the letter is usually written.
        case bank::Cmd::B:    { const double q = 1.0 - (1.0 - x / 15.0) * (1.0 - y / 15.0);
                                return String(int(std::lround(q * 100.0))) + "% " + dot + "hop row " + String(y); }
    }
    return {};
}

namespace {
/// In the enum's order, H included: the ranges and the shape every editor of
/// a command obeys (docs/COMMANDS_AND_TEMPO.md sections 2 and 34).
constexpr CommandInfo kCmdInfo[bank::kCmdCount] = {
    { 'A', "Table",         "slot 1-64, 0 stops",        1, { 0, 0 }, { 64, 0 },   { 1, 0 },   CmdShape::Small },
    { 'C', "Chord",         "x, y semitones 0-15",       2, { 0, 0 }, { 15, 15 },  { 3, 7 },   CmdShape::Nibbles },
    { 'D', "Delay",         "ticks",                     1, { 0, 0 }, { 255, 0 },  { 3, 0 },   CmdShape::Byte },
    { 'E', "Envelope",      "vol, y 0/8 hold, 1-7 down, 9-15 up", 2, { 0, 0 }, { 15, 15 }, { 12, 3 }, CmdShape::Nibbles },
    { 'F', "Frame",         "1-16",                      1, { 1, 0 }, { 16, 0 },   { 2, 0 },   CmdShape::Small },
    { 'G', "Groove",        "slot 1-16, 0 straight",     1, { 0, 0 }, { 16, 0 },   { 1, 0 },   CmdShape::Small },
    { 'H', "Hop",           "times (0 forever), row 1-16", 2, { 0, 1 }, { 15, 16 }, { 1, 1 },  CmdShape::Nibbles },
    { 'K', "Kill",          "after ticks",               1, { 0, 0 }, { 255, 0 },  { 4, 0 },   CmdShape::Byte },
    { 'L', "Slide",         "duration, 0 instant",       1, { 0, 0 }, { 255, 0 },  { 60, 0 },  CmdShape::Byte },
    { 'M', "Master vol",    "0-7, 8 keeps, 9-15 relative", 2, { 0, 0 }, { 15, 15 }, { 5, 5 },  CmdShape::Nibbles },
    { 'O', "Pan",           "off / L / R / LR",          1, { 0, 0 }, { 3, 0 },    { 1, 0 },   CmdShape::Small },
    { 'P', "Pitch bend",    "speed, signed; 0 holds",    1, { 0, 0 }, { 255, 0 },  { 244, 0 }, CmdShape::Byte },
    { 'R', "Retrigger",     "vol 1-7 up / 9-15 down, every y", 2, { 0, 0 }, { 15, 15 }, { 0, 3 }, CmdShape::Nibbles },
    { 'S', "Sweep",         "PU1: rate 0-7, NR10's low nibble (8-15 down); NOI: transpose, two's complement", 2, { 0, 0 }, { 15, 15 }, { 2, 2 }, CmdShape::Nibbles },
    { 'T', "Tempo",         "BPM 40-295",                1, { 40, 0 }, { 295, 0 }, { 120, 0 }, CmdShape::Byte },
    { 'V', "Vibrato",       "speed 1-15, depth in semitones", 2, { 0, 0 }, { 15, 15 }, { 8, 4 }, CmdShape::Nibbles },
    { 'W', "Wave",          "duty 0-3, or wave 1-64",    1, { 0, 0 }, { 64, 0 },   { 1, 0 },   CmdShape::Small },
    { 'Z', "Random add",    "0..x on x, 0..y on y",      2, { 0, 0 }, { 15, 15 },  { 15, 0 },  CmdShape::Nibbles },
    { 'B', "Chance",        "cell: two x/15 rolls; table: hop row y, x/16 of the time", 2, { 0, 0 }, { 15, 15 }, { 15, 0 }, CmdShape::Nibbles },
};
} // namespace

const CommandInfo* commandInfo(bank::Cmd c)
{
    const int i = int(c) - 1;   // Cmd::A == 1 .. Cmd::Z == 18, the table's order
    return i >= 0 && i < bank::kCmdCount ? &kCmdInfo[size_t(i)] : nullptr;
}

bank::Command defaultCommand(bank::Cmd c)
{
    bank::Command out;
    out.cmd = c;
    if (const auto* info = commandInfo(c)) { out.a = int16_t(info->def[0]); out.b = int16_t(info->def[1]); }
    return out;
}

/* ------- the two views of one byte (COMMANDS_AND_TEMPO.md section 34) ------- */

namespace {
/// The nibble H's row lands in: ChipBoy counts a table's rows from one and
/// the register counts from zero, so the byte carries row - 1.
int hopRowNibble(int row) { return std::clamp(row, 1, 16) - 1; }
}

int commandByte(const bank::Command& c)
{
    const auto* info = commandInfo(c.cmd);
    if (info == nullptr) return 0;
    switch (info->shape) {
        case CmdShape::Nibbles: {
            const int y = c.cmd == bank::Cmd::H ? hopRowNibble(c.b) : (c.b & 15);
            return ((c.a & 15) << 4) | (y & 15);
        }
        case CmdShape::Byte:
            // T wraps the way LSDj's does: 40-255 is 0x28-0xFF and 256-295
            // runs on through 0x00-0x27. Everything else is already a byte.
            return c.cmd == bank::Cmd::T ? (int(c.a) & 255) : (int(c.a) & 255);
        case CmdShape::Small:
            // A groove is a *slot*, and a slot counts from 00 in Hex (section
            // 52), so the byte is one less than the slot ChipBoy stores -- the
            // number then reads the same as the Grooves tab's, and the same as
            // LSDj's own G (section 69). "Straight" has no byte and no command.
            if (c.cmd == bank::Cmd::G) return std::max(0, int(c.a) - 1) & 255;
            return int(c.a) & 255;
    }
    return 0;
}

bool setCommandByte(bank::Command& c, int byte)
{
    const auto* info = commandInfo(c.cmd);
    if (info == nullptr || byte < 0 || byte > 255) return false;
    switch (info->shape) {
        case CmdShape::Nibbles: {
            const int x = (byte >> 4) & 15;
            const int y = c.cmd == bank::Cmd::H ? (byte & 15) + 1 : (byte & 15);
            if (x < info->lo[0] || x > info->hi[0] || y < info->lo[1] || y > info->hi[1]) return false;
            c.a = int16_t(x); c.b = int16_t(y); c.c = 0;
            return true;
        }
        case CmdShape::Byte: {
            int v = byte;
            if (c.cmd == bank::Cmd::T) v = byte >= 40 ? byte : byte + 256;   // the wrap, back again
            if (v < info->lo[0] || v > info->hi[0]) return false;
            c.a = int16_t(v); c.b = 0; c.c = 0;
            return true;
        }
        case CmdShape::Small: {
            // A G's byte is its slot counted from 00, so 06 is the sixth groove
            // the tab shows and LSDj's G06 types as itself (section 69).
            const int v = c.cmd == bank::Cmd::G ? byte + 1 : byte;
            if (v < info->lo[0] || v > info->hi[0]) return false;
            c.a = int16_t(v); c.b = 0; c.c = 0;
            return true;
        }
    }
    return false;
}

int commandShownValue(const bank::Command& c, int arg)
{
    const int v = arg == 0 ? int(c.a) : int(c.b);
    return c.cmd == bank::Cmd::P && arg == 0 ? signedByte(v) : v;
}

void commandShownRange(bank::Cmd cmd, int arg, int& lo, int& hi)
{
    const auto* info = commandInfo(cmd);
    const int a = std::clamp(arg, 0, 1);
    lo = info != nullptr ? info->lo[a] : 0;
    hi = info != nullptr ? info->hi[a] : 0;
    if (cmd == bank::Cmd::P && a == 0) { lo = -128; hi = 127; }   // the byte, read signed
}

bool setCommandShownValue(bank::Command& c, int arg, int shown)
{
    const auto* info = commandInfo(c.cmd);
    if (info == nullptr) return false;
    const int a = std::clamp(arg, 0, info->nargs - 1);
    int lo = 0, hi = 0;
    commandShownRange(c.cmd, a, lo, hi);
    if (shown < lo || shown > hi) return false;              // refused, not clamped
    const int stored = c.cmd == bank::Cmd::P && a == 0 ? (shown & 255) : shown;
    if (a == 0) c.a = int16_t(stored); else c.b = int16_t(stored);
    c.c = 0;                                                 // naming a value leaves the revert form
    return true;
}

String commandEntryText(const bank::Command& c, int arg)
{
    return String(commandShownValue(c, std::clamp(arg, 0, 1)));
}

String commandValueText(const bank::Command& c, bool hex)
{
    const auto* info = commandInfo(c.cmd);
    if (info == nullptr) return {};
    // Hex is the byte a playback ROM will carry -- one encoding, one export.
    if (hex) {
        const int b = commandByte(c);
        return String::toHexString(b).paddedLeft('0', 2).toUpperCase();
    }
    if (c.cmd == bank::Cmd::O) { static const char* n[] = { "\xe2\x80\x93", "L", "R", "LR" }; return String(CharPointer_UTF8(n[c.a & 3])); }
    String s;
    for (int i = 0; i < info->nargs; ++i) {
        if (i > 0) s += ",";
        s += String(commandShownValue(c, i));
    }
    return s;
}

bool commandAppliesTo(bank::Cmd c, ChannelKind kind)
{
    if (kind == ChannelKind::Any) return true;
    const bool pulse = kind == ChannelKind::Pulse1 || kind == ChannelKind::Pulse2;
    const bool wave = kind == ChannelKind::Wave;
    if (c == bank::Cmd::H) return false;                  // hop only means anything inside a table
    if (c == bank::Cmd::F) return wave;                   // frames belong to a wave
    if (c == bank::Cmd::S) return kind == ChannelKind::Pulse1;   // only PU1 has NR10
    // C, L, P, V and W all need a period the noise channel does not have.
    if (c == bank::Cmd::C || c == bank::Cmd::L || c == bank::Cmd::P || c == bank::Cmd::V || c == bank::Cmd::W) return pulse || wave;
    return true;
}

String channelPrefix(int channel) { return "ch" + String(channel + 1) + "_"; }
String channelParamId(int channel, const char* id) { return channelPrefix(channel) + id; }

SourceChoice decodeSource(int i)
{
    SourceChoice s;
    if (i <= 0) s.omni = true;
    else if (i >= 17) s.off = true;
    else s.midiChannel = i;
    return s;
}

void addGlobalParameters(AudioProcessorValueTreeState::ParameterLayout& L)
{
    L.add(choiceParam(ids::model, "Model", { "DMG", "CGB", "RAW" }, 0));
    L.add(intParam(ids::masterL, "Master Volume L", 0, 7, 7, [](int v, int) { return v == 0 ? String("0 (1/8)") : String(v); }));
    L.add(intParam(ids::masterR, "Master Volume R", 0, 7, 7, [](int v, int) { return v == 0 ? String("0 (1/8)") : String(v); }));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID(ids::trim, 1), "Output Trim",
          NormalisableRange<float>(-40.0f, 6.0f, 0.1f), -6.0f,
          AudioParameterFloatAttributes().withLabel("dB").withStringFromValueFunction([](float v, int) { return String(v, 1) + " dB"; })));
    // The two halves of the noise floor, independent switches (section 21):
    // the hiss and the frame hum, and the display's own line.
    L.add(boolParam(ids::noise, "Headphone Noise", true));
    L.add(boolParam(ids::lcd, "LCD Whine", true));
    L.add(choiceParam(ids::bassMod, "CGB Bass Mod", { "stock", "x10", "x47" }, 0));
    L.add(boolParam(ids::volEdges, "Volume Writes At Edges", false));
    L.add(boolParam(ids::declick, "De-click", false));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID(ids::declickMs, 1), "De-click ms",
          NormalisableRange<float>(0.5f, 5.0f, 0.5f), 2.0f, AudioParameterFloatAttributes().withLabel("ms")));
    L.add(boolParam(ids::soften, "Soften Master Pops", false));
    // Tempo (docs/COMMANDS_AND_TEMPO.md section 4). Ticks are always 24 per
    // beat; what a tick is worth is the only choice left.
    L.add(choiceParam(ids::tempoSource, "Tempo Source", { "Host", "Song" }, 0));
    L.add(intParam(ids::songTempo, "Song Tempo", 40, 255, 120, [](int v, int) { return String(v) + " BPM"; }));
    L.add(boolParam(ids::notesOnTick, "Quantize Notes To Ticks", false));
    L.add(boolParam(ids::linkMode, "Link Mode", false));
    L.add(boolParam(ids::hexDisplay, "Hex Display", true));   // Hex by default: it counts like LSDj (section 52)
}

void addChannelParameters(AudioProcessorValueTreeState::ParameterLayout& L, const String& px, ChannelKind kind, bool withSource)
{
    // The channel is a tracker row (docs/COMMANDS_AND_TEMPO.md section 3):
    // an instrument, a table, the few performance fields, and two commands.
    const bool wave = kind == ChannelKind::Wave;
    const String name = kind == ChannelKind::Pulse1 ? "PU1 " : kind == ChannelKind::Pulse2 ? "PU2 " : kind == ChannelKind::Wave ? "WAV " : kind == ChannelKind::Noise ? "NOI " : "";

    if (withSource) {
        StringArray src { "Omni" };
        for (int i = 1; i <= 16; ++i) src.add("MIDI " + String(i));
        src.add("Off");
        const int def = kind == ChannelKind::Pulse1 ? 0 : kind == ChannelKind::Pulse2 ? 2 : kind == ChannelKind::Wave ? 3 : 4;
        L.add(choiceParam(px + ids::source, name + "Source", src, def));
    }
    const int defInst = kind == ChannelKind::Pulse1 ? 1 : kind == ChannelKind::Pulse2 ? 3 : kind == ChannelKind::Wave ? 7 : kind == ChannelKind::Noise ? 11 : 1;
    L.add(intParam(px + ids::instrument, name + "Instrument", 0, 128, defInst, [](int v, int) { return v == 0 ? String("none") : String(v); }));
    L.add(intParam(px + ids::table, name + "Table", 0, 64, 0, [](int v, int) { return v == 0 ? String("inst") : String(v); }));
    if (wave)
        L.add(intParam(px + ids::level, name + "Level", 0, 4, 4, [](int v, int) { static const char* n[] = { "mute", "25%", "50%", "100%", "inst" }; return String(n[std::clamp(v, 0, 4)]); }));
    else
        L.add(intParam(px + ids::level, name + "Level", 0, 16, 16, [](int v, int) { return instText(v, 16); }));
    L.add(choiceParam(px + ids::pan, name + "Pan", { "off", "L", "R", "both", "inst" }, 4));
    L.add(intParam(px + ids::transpose, name + "Transpose", -60, 60, 0, [](int v, int) { return (v > 0 ? "+" : "") + String(v) + " st"; }));
    const char* typeIds[2] = { ids::cmd1Type, ids::cmd2Type };
    const char* xIds[2] = { ids::cmd1X, ids::cmd2X };
    const char* yIds[2] = { ids::cmd1Y, ids::cmd2Y };
    for (int i = 0; i < 2; ++i) {
        const String n = name + "CMD" + String(i + 1);
        L.add(choiceParam(px + typeIds[i], n, commandChoices(), 0));
        L.add(intParam(px + xIds[i], n + " x", 0, 255, 0));
        L.add(intParam(px + yIds[i], n + " y", 0, 255, 0));
    }
    L.add(boolParam(px + ids::liveFollow, name + "Live Follow", false));
    L.add(choiceParam(px + ids::velocityMode, name + "Velocity", { "start volume", "instrument bank", "ignored" }, 0));
    L.add(boolParam(px + ids::keyswitch, name + "Keyswitches", false));
}

void ChannelParamCache::bind(AudioProcessorValueTreeState& s, const String& px)
{
    auto g = [&](const char* id) { return s.getRawParameterValue(px + id); };
    source = g(ids::source); instrument = g(ids::instrument); table = g(ids::table); level = g(ids::level); pan = g(ids::pan);
    transpose = g(ids::transpose);
    cmdType[0] = g(ids::cmd1Type); cmdX[0] = g(ids::cmd1X); cmdY[0] = g(ids::cmd1Y);
    cmdType[1] = g(ids::cmd2Type); cmdX[1] = g(ids::cmd2X); cmdY[1] = g(ids::cmd2Y);
    liveFollow = g(ids::liveFollow); velocityMode = g(ids::velocityMode); keyswitch = g(ids::keyswitch);
}

driver::ChannelParams ChannelParamCache::read(ChannelKind kind) const
{
    driver::ChannelParams p;
    p.instrument = uint8_t(std::clamp(paramInt(instrument), 0, 128));
    p.table = uint8_t(std::clamp(paramInt(table), 0, 64));
    const int lv = paramInt(level, 16);
    if (kind == ChannelKind::Wave) p.level = lv >= 4 ? 255 : uint8_t(lv);
    else p.level = lv >= 16 ? 255 : uint8_t(lv);
    const int pn = paramInt(pan, 4); p.pan = pn >= 4 ? 255 : uint8_t(pn);
    p.transpose = int8_t(std::clamp(paramInt(transpose), -60, 60));
    for (int i = 0; i < 2; ++i) {
        p.cmd[i].cmd = cmdFromChoice(paramInt(cmdType[i]));
        p.cmd[i].a = int16_t(std::clamp(paramInt(cmdX[i]), 0, 255));
        p.cmd[i].b = int16_t(std::clamp(paramInt(cmdY[i]), 0, 255));
    }
    p.liveFollow = paramInt(liveFollow) != 0;
    p.velocityMode = uint8_t(std::clamp(paramInt(velocityMode), 0, 2));
    p.keyswitch = paramInt(keyswitch) != 0;
    return p;
}

} // namespace chipboy::plugin
