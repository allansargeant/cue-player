#pragma once

#include <juce_core/juce_core.h>

#include <cmath>
#include <cstdio>

/** Minimal assertion helpers shared by the test files.

    Deliberately not a test framework: these tests run as one console binary in CI, and a
    failure needs to print what it got and what it expected, which is all of it.
*/
namespace cptest
{

inline int checks = 0;
inline int failures = 0;

inline void section (const juce::String& name)
{
    std::printf ("%s\n", name.toRawUTF8());
}

inline void check (bool condition, const juce::String& what)
{
    ++checks;

    if (! condition)
    {
        ++failures;
        std::printf ("  FAIL  %s\n", what.toRawUTF8());
    }
}

inline void checkNear (double actual, double expected, double tolerance, const juce::String& what)
{
    ++checks;

    if (! (std::abs (actual - expected) <= tolerance))
    {
        ++failures;
        std::printf ("  FAIL  %s  (got %.6f, expected %.6f, tolerance %.6f)\n",
                     what.toRawUTF8(), actual, expected, tolerance);
    }
}

inline void checkEqual (const juce::String& actual, const juce::String& expected,
                        const juce::String& what)
{
    ++checks;

    if (actual != expected)
    {
        ++failures;
        std::printf ("  FAIL  %s  (got \"%s\", expected \"%s\")\n",
                     what.toRawUTF8(), actual.toRawUTF8(), expected.toRawUTF8());
    }
}

} // namespace cptest
