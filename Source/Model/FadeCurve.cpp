#include "Model/FadeCurve.h"

#include <cmath>

namespace cp
{

juce::StringArray fadeShapeNames()
{
    return { "Linear", "Equal power", "Exponential", "Logarithmic", "S-curve" };
}

juce::String toString (FadeShape shape)
{
    switch (shape)
    {
        case FadeShape::linear:      return "linear";
        case FadeShape::equalPower:  return "equalPower";
        case FadeShape::exponential: return "exponential";
        case FadeShape::logarithmic: return "logarithmic";
        case FadeShape::sCurve:      return "sCurve";
    }

    return "equalPower";
}

FadeShape fadeShapeFromString (const juce::String& s)
{
    if (s == "linear")      return FadeShape::linear;
    if (s == "exponential") return FadeShape::exponential;
    if (s == "logarithmic") return FadeShape::logarithmic;
    if (s == "sCurve")      return FadeShape::sCurve;

    return FadeShape::equalPower;
}

float evaluateFadeIn (FadeShape shape, float t) noexcept
{
    t = juce::jlimit (0.0f, 1.0f, t);

    switch (shape)
    {
        case FadeShape::linear:
            return t;

        case FadeShape::equalPower:
            // sin sweep from 0 to pi/2: gain^2 + (1-gain)^2 stays constant across a
            // symmetric crossfade, so the sum of two uncorrelated sources holds level.
            return std::sin (t * juce::MathConstants<float>::halfPi);

        case FadeShape::exponential:
            // t^2 — slow start. Not a true dB-linear exponential (which never reaches
            // silence); squaring gives a usable "sneak it in" shape that hits exactly 0.
            return t * t;

        case FadeShape::logarithmic:
            return 1.0f - (1.0f - t) * (1.0f - t);

        case FadeShape::sCurve:
            return t * t * (3.0f - 2.0f * t);
    }

    return t;
}

} // namespace cp
