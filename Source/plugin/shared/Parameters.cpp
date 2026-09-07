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
    L.add(choiceParam(ids::tickSource, "Tick Source", { "Host", "V-blank", "Custom" }, 0));
    L.add(intParam(ids::ticksPerBeat, "Ticks Per Beat", 1, 48, 24));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID(ids::tickHz, 1), "Tick Rate",
          NormalisableRange<float>(1.0f, 240.0f, 0.5f), 60.0f, AudioParameterFloatAttributes().withLabel("Hz")));
    L.add(boolParam(ids::linkMode, "Link Mode", false));
    L.add(boolParam(ids::hexDisplay, "Hex Display", false));
}

void addChannelParameters(AudioProcessorValueTreeState::ParameterLayout& L, const String& px, ChannelKind kind, bool withSource)
{
    const bool pulse = kind == ChannelKind::Pulse1 || kind == ChannelKind::Pulse2 || kind == ChannelKind::Any;
    const bool wave = kind == ChannelKind::Wave || kind == ChannelKind::Any;
    const bool noise = kind == ChannelKind::Noise || kind == ChannelKind::Any;
    const bool pu1 = kind == ChannelKind::Pulse1 || kind == ChannelKind::Any;
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
    if (wave && kind != ChannelKind::Any)
        L.add(intParam(px + ids::level, name + "Level", 0, 4, 4, [](int v, int) { static const char* n[] = { "mute", "25%", "50%", "100%", "inst" }; return String(n[std::clamp(v, 0, 4)]); }));
    else
        L.add(intParam(px + ids::level, name + "Level", 0, 16, 16, [](int v, int) { return instText(v, 16); }));
    L.add(choiceParam(px + ids::pan, name + "Pan", { "off", "L", "R", "both", "inst" }, 4));
    if (wave) {
        L.add(intParam(px + ids::wave, name + "Wave", 0, 64, 0, [](int v, int) { return v == 0 ? String("inst") : String(v); }));
        L.add(intParam(px + ids::frame, name + "Frame", 0, 16, 0, [](int v, int) { return v == 0 ? String("auto") : String(v); }));
    }
    L.add(intParam(px + ids::transpose, name + "Transpose", -60, 60, 0, [](int v, int) { return (v > 0 ? "+" : "") + String(v) + " st"; }));
    L.add(intParam(px + ids::detune, name + "Detune", -128, 127, 0));
    L.add(intParam(px + ids::vibSpeed, name + "Vibrato Speed", 0, 15, 0, [](int v, int) { return v == 0 ? String("inst") : String(v); }));
    L.add(intParam(px + ids::vibDepth, name + "Vibrato Depth", 0, 16, 16, [](int v, int) { return instText(v, 16); }));
    L.add(intParam(px + ids::arp, name + "Arpeggio", 0, 64, 0, [](int v, int) { return v == 0 ? String("none") : String(v); }));
    if (pulse || noise) {
        L.add(intParam(px + ids::envVol, name + "Envelope Volume", 0, 16, 16, [](int v, int) { return instText(v, 16); }));
        L.add(choiceParam(px + ids::envDir, name + "Envelope Direction", { "down", "up", "inst" }, 2));
        L.add(intParam(px + ids::envRate, name + "Envelope Rate", 0, 8, 8, [](int v, int) { return instText(v, 8); }));
    }
    if (pulse) L.add(choiceParam(px + ids::duty, name + "Duty", { "12.5%", "25%", "50%", "75%", "inst" }, 4));
    if (pu1) {
        L.add(intParam(px + ids::sweepRate, name + "Sweep Rate", 0, 8, 8, [](int v, int) { return instText(v, 8); }));
        L.add(choiceParam(px + ids::sweepDir, name + "Sweep Direction", { "up", "down", "inst" }, 2));
        L.add(intParam(px + ids::sweepShift, name + "Sweep Shift", 0, 8, 8, [](int v, int) { return instText(v, 8); }));
    }
    if (noise) L.add(choiceParam(px + ids::lfsr, name + "LFSR", { "15-bit", "7-bit", "inst" }, 2));
    L.add(boolParam(px + ids::liveFollow, name + "Live Follow", false));
    L.add(choiceParam(px + ids::velocityMode, name + "Velocity", { "start volume", "instrument bank", "ignored" }, 0));
    L.add(boolParam(px + ids::keyswitch, name + "Keyswitches", false));
}

void ChannelParamCache::bind(AudioProcessorValueTreeState& s, const String& px)
{
    auto g = [&](const char* id) { return s.getRawParameterValue(px + id); };
    source = g(ids::source); instrument = g(ids::instrument); table = g(ids::table); level = g(ids::level); pan = g(ids::pan);
    wave = g(ids::wave); frame = g(ids::frame); transpose = g(ids::transpose); detune = g(ids::detune);
    vibSpeed = g(ids::vibSpeed); vibDepth = g(ids::vibDepth); arp = g(ids::arp);
    envVol = g(ids::envVol); envDir = g(ids::envDir); envRate = g(ids::envRate); duty = g(ids::duty);
    sweepRate = g(ids::sweepRate); sweepDir = g(ids::sweepDir); sweepShift = g(ids::sweepShift); lfsr = g(ids::lfsr);
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
    p.wave = uint8_t(std::clamp(paramInt(wave), 0, 64));
    p.frame = uint8_t(std::clamp(paramInt(frame), 0, 16));
    p.transpose = int8_t(std::clamp(paramInt(transpose), -60, 60));
    p.detune = int16_t(std::clamp(paramInt(detune), -128, 127));
    const int vs = paramInt(vibSpeed); p.vibSpeed = vs == 0 ? 255 : uint8_t(vs);
    const int vd = paramInt(vibDepth, 16); p.vibDepth = vd >= 16 ? 255 : uint8_t(vd);
    p.arp = uint8_t(std::clamp(paramInt(arp), 0, 64));
    const int ev = paramInt(envVol, 16); p.envVol = ev >= 16 ? 255 : uint8_t(ev);
    const int ed = paramInt(envDir, 2); p.envDir = ed >= 2 ? 255 : uint8_t(ed);
    const int er = paramInt(envRate, 8); p.envRate = er >= 8 ? 255 : uint8_t(er);
    const int du = paramInt(duty, 4); p.duty = du >= 4 ? 255 : uint8_t(du);
    const int sr = paramInt(sweepRate, 8); p.sweepRate = sr >= 8 ? 255 : uint8_t(sr);
    const int sd = paramInt(sweepDir, 2); p.sweepDir = sd >= 2 ? 255 : uint8_t(sd);
    const int ss = paramInt(sweepShift, 8); p.sweepShift = ss >= 8 ? 255 : uint8_t(ss);
    const int lf = paramInt(lfsr, 2); p.lfsr = lf >= 2 ? 255 : uint8_t(lf);
    p.liveFollow = paramInt(liveFollow) != 0;
    p.velocityMode = uint8_t(std::clamp(paramInt(velocityMode), 0, 2));
    p.keyswitch = paramInt(keyswitch) != 0;
    return p;
}

} // namespace chipboy::plugin
