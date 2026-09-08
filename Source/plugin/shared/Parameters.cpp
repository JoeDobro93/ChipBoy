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
constexpr bank::Cmd kLaneCmds[] = {
    bank::Cmd::A, bank::Cmd::C, bank::Cmd::D, bank::Cmd::E, bank::Cmd::F, bank::Cmd::G,
    bank::Cmd::K, bank::Cmd::L, bank::Cmd::M, bank::Cmd::O, bank::Cmd::P, bank::Cmd::R,
    bank::Cmd::S, bank::Cmd::T, bank::Cmd::V, bank::Cmd::W, bank::Cmd::Z,
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
        case bank::Cmd::H:    return x == 0 ? String("stop") : "step " + String(x);
        case bank::Cmd::K:    return "after " + String(x);
        case bank::Cmd::L:    return x == 0 ? String("instant") : "over " + String(x);
        case bank::Cmd::M:    return "L " + masterSideText(x) + dot + "R " + masterSideText(y);
        case bank::Cmd::O:    { static const char* n[] = { "off", "L", "R", "both" }; return n[x & 3]; }
        case bank::Cmd::P:    { const int v = x - 128; return v == 0 ? String("hold") : (v > 0 ? "+" : "") + String(v); }
        case bank::Cmd::R:    { const String v = retrigStepText(x); return (y == 0 ? String("once") : "every " + String(y)) + (v.isEmpty() ? String() : dot + v); }
        case bank::Cmd::S:    return "rate " + String(x & 7) + dot + "shift " + String(y & 7) + ((x & 128) ? String(" down") : String());
        case bank::Cmd::T:    return String(x) + " BPM";
        case bank::Cmd::V:    return x == 0 ? String("off") : "speed " + String(x) + dot + vibDepthText(y);
        case bank::Cmd::W:    return "duty/wave " + String(x);
        case bank::Cmd::Z:    return "add 0-" + String(x) + (y ? dot + "0-" + String(y) : String());
    }
    return {};
}

namespace {
/// In the enum's order, H included: the ranges every editor of a command
/// obeys (docs/COMMANDS_AND_TEMPO.md section 2).
constexpr CommandInfo kCmdInfo[bank::kCmdCount] = {
    { 'A', "Table",         "slot 1-64, 0 stops",        1, { 0, 0 }, { 64, 0 },    { 1, 0 } },
    { 'C', "Chord",         "x, y semitones",            2, { 0, 0 }, { 60, 60 },   { 3, 7 } },
    { 'D', "Delay",         "ticks",                     1, { 0, 0 }, { 255, 0 },   { 3, 0 } },
    { 'E', "Envelope",      "vol, y 0/8 hold, 1-7 down, 9-15 up", 2, { 0, 0 }, { 15, 15 }, { 12, 3 } },
    { 'F', "Frame",         "1-16",                      1, { 1, 0 }, { 16, 0 },    { 2, 0 } },
    { 'G', "Groove",        "slot 1-16, 0 straight",     1, { 0, 0 }, { 16, 0 },    { 1, 0 } },
    { 'H', "Hop",           "step 1-16, 0 stops",        1, { 0, 0 }, { 16, 0 },    { 1, 0 } },
    { 'K', "Kill",          "after ticks",               1, { 0, 0 }, { 255, 0 },   { 4, 0 } },
    { 'L', "Slide",         "duration, 0 instant",       1, { 0, 0 }, { 255, 0 },   { 60, 0 } },
    { 'M', "Master vol",    "0-7, 8 keeps, 9-15 relative", 2, { 0, 0 }, { 15, 15 }, { 5, 5 } },
    { 'O', "Pan",           "off / L / R / LR",          1, { 0, 0 }, { 3, 0 },     { 1, 0 } },
    { 'P', "Pitch bend",    "speed, 128 holds",          1, { 0, 0 }, { 255, 0 },   { 116, 0 } },
    { 'R', "Retrigger",     "vol 1-7 up / 9-15 down, every y", 2, { 0, 0 }, { 15, 255 }, { 0, 3 } },
    { 'S', "Sweep",         "rate, shift (x>=128 down)", 2, { 0, 0 }, { 255, 7 },   { 2, 2 } },
    { 'T', "Tempo",         "BPM 40-255",                1, { 40, 0 }, { 255, 0 },  { 120, 0 } },
    { 'V', "Vibrato",       "speed 1-15, depth in semitones", 2, { 0, 0 }, { 15, 15 }, { 8, 4 } },
    { 'W', "Wave",          "duty 0-3, or wave 1-64",    1, { 0, 0 }, { 64, 0 },    { 1, 0 } },
    { 'Z', "Random add",    "0..x on x, 0..y on y",      2, { 0, 0 }, { 255, 255 }, { 15, 0 } },
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
    L.add(boolParam(ids::noise, "Headphone Noise", true));
    L.add(boolParam(ids::lcd, "LCD On", true));
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
    L.add(boolParam(ids::hexDisplay, "Hex Display", false));
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
