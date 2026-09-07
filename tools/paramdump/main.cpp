// chipboy_paramdump -- print the main plugin's parameters in host index
// order: "index<TAB>id<TAB>name<TAB>min<TAB>max<TAB>default<TAB>steps".
// The Reaper demo generator and the docs use this; JUCE exposes parameters
// to VST3 hosts in exactly this order (the bypass and MIDI CC parameters
// the wrapper adds come after).
#include "plugin/main/ChipBoyProcessor.h"

#include <cstdio>

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    chipboy::plugin::ChipBoyProcessor p;
    int index = 0;
    for (auto* param : p.getParameters()) {
        auto* r = dynamic_cast<juce::RangedAudioParameter*>(param);
        const auto& range = r->getNormalisableRange();
        const juce::String id = r->getParameterID();
        std::printf("%d\t%s\t%s\t%g\t%g\t%g\t%d\n", index++, id.toRawUTF8(), r->getName(64).toRawUTF8(),
                    double(range.start), double(range.end), double(r->convertFrom0to1(r->getDefaultValue())), r->getNumSteps());
    }
    return 0;
}
