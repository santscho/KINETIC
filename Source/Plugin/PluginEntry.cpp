#include "PluginProcessor.h"

// Each format wrapper (AU / VST3) calls this to build its processing instance.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kinetic::KineticAudioProcessor();
}
