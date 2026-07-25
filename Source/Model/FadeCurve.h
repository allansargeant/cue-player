#pragma once

#include <juce_core/juce_core.h>

namespace cp
{

/** Shapes available for automatic fade-ins, fade-outs and crossfades.

    Every shape is defined as a fade-*in* curve: a function mapping a normalised position
    t in [0, 1] onto a linear gain in [0, 1], with f(0) = 0 and f(1) = 1. Fade-outs simply
    evaluate the same function at (1 - t), which keeps a symmetric fade pair well behaved.
*/
enum class FadeShape
{
    linear = 0,     ///< Straight line in linear gain. Sounds fast at the top, slow at the bottom.
    equalPower,     ///< sin/cos law. Constant power through a crossfade — the usual default.
    exponential,    ///< Slow start, fast finish. Good for sneaking material in.
    logarithmic,    ///< Fast start, slow finish. Good for long, gentle tails.
    sCurve          ///< Smoothstep. Gentle at both ends, quick through the middle.
};

/** Human-readable names, indexed by FadeShape. */
juce::StringArray fadeShapeNames();

juce::String toString (FadeShape shape);
FadeShape fadeShapeFromString (const juce::String& s);

/** Evaluates a fade-in curve. @p t is clamped to [0, 1]. Returns a linear gain. */
float evaluateFadeIn (FadeShape shape, float t) noexcept;

/** Evaluates the matching fade-out curve, i.e. evaluateFadeIn (shape, 1 - t). */
inline float evaluateFadeOut (FadeShape shape, float t) noexcept
{
    return evaluateFadeIn (shape, 1.0f - t);
}

} // namespace cp
