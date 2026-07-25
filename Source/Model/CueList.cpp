#include "Model/CueList.h"

namespace cp
{

Cue* CueList::get (int index) noexcept
{
    return juce::isPositiveAndBelow (index, size()) ? &cues[(size_t) index] : nullptr;
}

const Cue* CueList::get (int index) const noexcept
{
    return juce::isPositiveAndBelow (index, size()) ? &cues[(size_t) index] : nullptr;
}

int CueList::indexOfID (const juce::Uuid& id) const noexcept
{
    for (int i = 0; i < size(); ++i)
        if (cues[(size_t) i].id == id)
            return i;

    return -1;
}

Cue* CueList::findByID (const juce::Uuid& id) noexcept
{
    return get (indexOfID (id));
}

const Cue* CueList::findByID (const juce::Uuid& id) const noexcept
{
    return get (indexOfID (id));
}

int CueList::insert (Cue cue, int index)
{
    if (! juce::isPositiveAndBelow (index, size() + 1))
        index = size();

    cues.insert (cues.begin() + index, std::move (cue));

    // Keep the operator's positions pointing at the same cues they were before.
    if (selectedIndex >= index) ++selectedIndex;
    if (standbyIndex  >= index) ++standbyIndex;

    clampPositions();
    sendChangeMessage();
    return index;
}

void CueList::remove (int index)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return;

    cues.erase (cues.begin() + index);

    if (selectedIndex > index) --selectedIndex;
    if (standbyIndex  > index) --standbyIndex;

    clampPositions();
    sendChangeMessage();
}

void CueList::removeByID (const juce::Uuid& id)
{
    remove (indexOfID (id));
}

void CueList::move (int fromIndex, int toIndex)
{
    if (! juce::isPositiveAndBelow (fromIndex, size()) || fromIndex == toIndex)
        return;

    toIndex = juce::jlimit (0, size() - 1, toIndex);

    const auto selectedID = get (selectedIndex) != nullptr ? get (selectedIndex)->id : juce::Uuid::null();
    const auto standbyID  = get (standbyIndex)  != nullptr ? get (standbyIndex)->id  : juce::Uuid::null();

    auto moved = std::move (cues[(size_t) fromIndex]);
    cues.erase (cues.begin() + fromIndex);
    cues.insert (cues.begin() + toIndex, std::move (moved));

    // Reordering should not move the operator's cursor onto a different cue.
    if (! selectedID.isNull()) selectedIndex = indexOfID (selectedID);
    if (! standbyID.isNull())  standbyIndex  = indexOfID (standbyID);

    clampPositions();
    sendChangeMessage();
}

void CueList::replace (int index, Cue cue)
{
    if (! juce::isPositiveAndBelow (index, size()))
        return;

    cues[(size_t) index] = std::move (cue);
    sendChangeMessage();
}

bool CueList::modify (int index, const std::function<void (Cue&)>& fn)
{
    if (! juce::isPositiveAndBelow (index, size()) || fn == nullptr)
        return false;

    fn (cues[(size_t) index]);
    sendChangeMessage();
    return true;
}

bool CueList::modifyByID (const juce::Uuid& id, const std::function<void (Cue&)>& fn)
{
    return modify (indexOfID (id), fn);
}

void CueList::clear()
{
    cues.clear();
    selectedIndex = -1;
    standbyIndex  = -1;
    sendChangeMessage();
}

juce::String CueList::suggestNextNumber() const
{
    int highest = 0;

    for (const auto& c : cues)
        highest = juce::jmax (highest, c.number.getIntValue());

    return juce::String (highest + 1);
}

void CueList::setSelectedIndex (int index)
{
    const auto clamped = isEmpty() ? -1 : juce::jlimit (-1, size() - 1, index);

    if (clamped != selectedIndex)
    {
        selectedIndex = clamped;
        sendChangeMessage();
    }
}

void CueList::setStandbyIndex (int index)
{
    const auto clamped = isEmpty() ? -1 : juce::jlimit (-1, size() - 1, index);

    if (clamped != standbyIndex)
    {
        standbyIndex = clamped;
        sendChangeMessage();
    }
}

void CueList::advanceStandby()
{
    if (isEmpty())
        return;

    // Deliberately stops on the last cue rather than wrapping: an accidental extra GO at
    // the end of a show should do nothing, not restart the top of the list.
    if (standbyIndex < size() - 1)
        setStandbyIndex (standbyIndex + 1);
    else
        setStandbyIndex (size() - 1);
}

const Cue* CueList::getStandbyCue() const noexcept
{
    return get (standbyIndex);
}

const Cue* CueList::resolveLinkTarget (int fromIndex) const noexcept
{
    const auto* from = get (fromIndex);

    if (from == nullptr)
        return nullptr;

    if (! from->link.targetsNextCue())
        return findByID (from->link.target);

    return get (fromIndex + 1);
}

void CueList::clampPositions()
{
    if (isEmpty())
    {
        selectedIndex = -1;
        standbyIndex  = -1;
        return;
    }

    selectedIndex = juce::jlimit (-1, size() - 1, selectedIndex);
    standbyIndex  = juce::jlimit (-1, size() - 1, standbyIndex);
}

//==============================================================================
juce::var CueList::toVar (const juce::File& showDirectory) const
{
    juce::Array<juce::var> arr;

    for (const auto& c : cues)
        arr.add (c.toVar (showDirectory));

    return arr;
}

void CueList::restoreFromVar (const juce::var& v, const juce::File& showDirectory)
{
    cues.clear();

    if (const auto* arr = v.getArray())
        for (const auto& item : *arr)
            cues.push_back (Cue::fromVar (item, showDirectory));

    selectedIndex = isEmpty() ? -1 : 0;
    standbyIndex  = isEmpty() ? -1 : 0;

    sendChangeMessage();
}

} // namespace cp
