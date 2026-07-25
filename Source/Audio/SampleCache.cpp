#include "Audio/SampleCache.h"

namespace cp
{

SampleCache::SampleCache()
    : juce::Thread ("Cue file loader")
{
    formats.registerBasicFormats();
    startThread (juce::Thread::Priority::normal);
}

SampleCache::~SampleCache()
{
    signalThreadShouldExit();
    workAvailable.signal();
    stopThread (5000);
}

void SampleCache::setSampleRate (double newRate)
{
    if (newRate <= 0.0 || std::abs (newRate - sampleRate.load()) < 1.0e-9)
        return;

    sampleRate.store (newRate);

    juce::Array<juce::File> toReload;

    {
        const juce::ScopedLock sl (lock);

        for (const auto& [key, entry] : entries)
            if (entry.status != Status::failed)
                toReload.add (juce::File (key));

        entries.clear();
        queue.clear();
    }

    for (const auto& f : toReload)
        request (f);

    sendChangeMessage();
}

void SampleCache::request (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    {
        const juce::ScopedLock sl (lock);

        const auto key = keyFor (file);

        if (const auto it = entries.find (key); it != entries.end())
            if (it->second.status == Status::loaded || it->second.status == Status::loading)
                return;

        entries[key] = { Status::loading, {}, {} };
        queue.addIfNotAlreadyThere (file);
    }

    workAvailable.signal();
    sendChangeMessage();
}

std::shared_ptr<SampleSource> SampleCache::loadImmediately (const juce::File& file, juce::String& error)
{
    jassert (! juce::MessageManager::getInstance()->isThisTheMessageThread()
             || true);   // Fine on the message thread; never call this from audio.

    if (auto existing = get (file))
        return existing;

    {
        const juce::ScopedLock sl (lock);
        entries[keyFor (file)] = { Status::loading, {}, {} };
    }

    auto source = SampleSource::load (file, sampleRate.load(), formats, error);

    {
        const juce::ScopedLock sl (lock);
        entries[keyFor (file)] = { source != nullptr ? Status::loaded : Status::failed,
                                   source, error };
    }

    sendChangeMessage();
    return source;
}

std::shared_ptr<SampleSource> SampleCache::get (const juce::File& file) const
{
    const juce::ScopedLock sl (lock);

    if (const auto it = entries.find (keyFor (file)); it != entries.end())
        return it->second.source;

    return {};
}

SampleCache::Entry SampleCache::getEntry (const juce::File& file) const
{
    const juce::ScopedLock sl (lock);

    if (const auto it = entries.find (keyFor (file)); it != entries.end())
        return it->second;

    return {};
}

void SampleCache::retainOnly (const juce::Array<juce::File>& keep)
{
    const juce::ScopedLock sl (lock);

    for (auto it = entries.begin(); it != entries.end();)
    {
        if (keep.contains (juce::File (it->first)))
            ++it;
        else
            it = entries.erase (it);
    }
}

void SampleCache::clear()
{
    {
        const juce::ScopedLock sl (lock);
        entries.clear();
        queue.clear();
    }

    sendChangeMessage();
}

juce::int64 SampleCache::getMemoryUsage() const
{
    const juce::ScopedLock sl (lock);

    juce::int64 total = 0;

    for (const auto& [key, entry] : entries)
        if (entry.source != nullptr)
            total += entry.source->getMemoryUsage();

    return total;
}

int SampleCache::getNumPending() const
{
    const juce::ScopedLock sl (lock);
    return queue.size();
}

juce::String SampleCache::getWildcardFilter()
{
    return formats.getWildcardForAllFormats();
}

void SampleCache::run()
{
    while (! threadShouldExit())
    {
        juce::File next;

        {
            const juce::ScopedLock sl (lock);

            if (! queue.isEmpty())
            {
                next = queue.getFirst();
                queue.remove (0);
            }
        }

        if (next == juce::File())
        {
            workAvailable.wait (500);
            continue;
        }

        juce::String error;
        auto source = SampleSource::load (next, sampleRate.load(), formats, error);

        {
            const juce::ScopedLock sl (lock);

            // A rate change may have wiped the entry out from under us; if it did, this
            // result is stale and re-adding it would resurrect the wrong sample rate.
            if (const auto it = entries.find (keyFor (next)); it != entries.end())
                it->second = { source != nullptr ? Status::loaded : Status::failed, source, error };
        }

        sendChangeMessage();
    }
}

} // namespace cp
