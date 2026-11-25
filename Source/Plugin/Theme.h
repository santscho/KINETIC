#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kinetic::theme
{

inline const juce::Colour background { 0xff0e1014 };
inline const juce::Colour panel      { 0xff171b22 };
inline const juce::Colour raised     { 0xff222836 };
inline const juce::Colour accent     { 0xff4ff0c0 };
inline const juce::Colour text       { 0xffe7ecf3 };
inline const juce::Colour muted      { 0xff7c8797 };
inline const juce::Colour amber      { 0xffffb457 };
inline const juce::Colour violet     { 0xff9d8cff };

inline juce::Font heading (float height)
{
    return juce::Font (juce::FontOptions (height).withStyle ("Bold"));
}

inline juce::Font mono (float height)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(),
                                          height, juce::Font::plain));
}

} // namespace kinetic::theme
