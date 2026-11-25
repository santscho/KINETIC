#include <juce_core/juce_core.h>

#include "Core/Sequencer/PatternState.h"

namespace kinetic
{

class PatternStateTests final : public juce::UnitTest
{
public:
    PatternStateTests() : juce::UnitTest ("PatternState", "kinetic") {}

    void runTest() override
    {
        testRoundTrip();
        testDefaultStepsAreNotWritten();
        testRestoreReplacesTheWholePattern();
        testMissingPatternIsIgnored();
        testSelectedLaneSurvives();
        testSurvivesTheTextForm();
        testUnknownFormatIsRefused();
        testMissingFormatAttributeIsVersionOne();
    }

private:
    static bool lanesMatch (const Lane& a, const Lane& b)
    {
        const auto& x = a.settings;
        const auto& y = b.settings;

        if (x.enabled != y.enabled || x.type != y.type || x.midiChannel != y.midiChannel
             || x.rootNote != y.rootNote || x.modController != y.modController
             || x.clock.numerator != y.clock.numerator || x.clock.denominator != y.clock.denominator
             || x.clock.modifier != y.clock.modifier || x.gateLength != y.gateLength
             || x.pitchLength != y.pitchLength || x.velocityLength != y.velocityLength
             || x.modLength != y.modLength || x.direction != y.direction
             || x.euclidEnabled != y.euclidEnabled || x.euclidSteps != y.euclidSteps
             || x.euclidPulses != y.euclidPulses || x.euclidRotation != y.euclidRotation
             || x.useGlobalScale != y.useGlobalScale || x.scale != y.scale
             || x.scaleRoot != y.scaleRoot || x.slideMode != y.slideMode
             || ! juce::approximatelyEqual (x.bendRangeSemitones, y.bendRangeSemitones)
             || ! juce::approximatelyEqual (x.slideOverlapMs, y.slideOverlapMs) || x.accentCC != y.accentCC
             || x.accentCCValue != y.accentCCValue || x.accentVelocityBoost != y.accentVelocityBoost)
            return false;

        for (std::size_t i = 0; i < a.steps.size(); ++i)
        {
            const auto& p = a.steps[i];
            const auto& q = b.steps[i];

            if (p.trigger != q.trigger || p.velocity != q.velocity || p.gatePercent != q.gatePercent
                 || p.microTiming != q.microTiming || p.probability != q.probability
                 || p.condition != q.condition || p.ratchets != q.ratchets
                 || p.ratchetRamp != q.ratchetRamp || p.slide != q.slide || p.accent != q.accent
                 || p.pitch != q.pitch || p.mod != q.mod)
                return false;
        }

        return true;
    }

    static bool patternsMatch (const Pattern& a, const Pattern& b)
    {
        if (a.globalScale != b.globalScale || a.globalRoot != b.globalRoot
             || a.customScaleMask != b.customScaleMask)
            return false;

        for (std::size_t i = 0; i < a.lanes.size(); ++i)
            if (! lanesMatch (a.lanes[i], b.lanes[i]))
                return false;

        return true;
    }

    /** A pattern that differs from the defaults in every field the format stores. */
    static Pattern makeBusyPattern()
    {
        Pattern pattern;
        pattern.globalScale = ScaleType::dorian;
        pattern.globalRoot = 3;
        pattern.customScaleMask = 0x0295;

        for (int laneIndex = 0; laneIndex < maxLanes; ++laneIndex)
        {
            auto& lane = pattern.lanes[static_cast<std::size_t> (laneIndex)];
            auto& settings = lane.settings;

            settings.enabled = laneIndex % 2 == 0;
            settings.type = laneIndex == 3 ? LaneType::cc : LaneType::note;
            settings.midiChannel = 1 + laneIndex;
            settings.rootNote = 48 + laneIndex;
            settings.clock = { 1, 4, laneIndex % 3 == 0 ? ClockModifier::dotted
                                 : laneIndex % 3 == 1 ? ClockModifier::triplet
                                                      : ClockModifier::straight };
            settings.gateLength = 1 + laneIndex * 3;
            settings.pitchLength = 2 + laneIndex;
            settings.velocityLength = 3 + laneIndex;
            settings.modLength = 4 + laneIndex;
            settings.modController = 70 + laneIndex;
            settings.direction = static_cast<PlayDirection> (laneIndex % 5);
            settings.euclidEnabled = laneIndex % 2 == 1;
            settings.euclidSteps = 5 + laneIndex;
            settings.euclidPulses = laneIndex;
            settings.euclidRotation = laneIndex % 3;
            settings.useGlobalScale = laneIndex % 2 == 0;
            settings.scale = static_cast<ScaleType> (laneIndex % 7);
            settings.scaleRoot = laneIndex % 12;
            settings.slideMode = static_cast<SlideMode> (laneIndex % 3);
            settings.bendRangeSemitones = 1.0 + laneIndex;
            settings.slideOverlapMs = 5.0 + laneIndex;
            settings.accentCC = 60 + laneIndex;
            settings.accentCCValue = 100 + laneIndex;
            settings.accentVelocityBoost = 10 + laneIndex;

            for (int stepIndex = 0; stepIndex < maxSteps; ++stepIndex)
            {
                auto& step = lane.steps[static_cast<std::size_t> (stepIndex)];

                step.trigger = (stepIndex + laneIndex) % 3 == 0;
                step.velocity = static_cast<juce::uint8> ((stepIndex * 3 + laneIndex) % 128);
                step.gatePercent = static_cast<juce::uint8> (2 + (stepIndex + laneIndex) % 99);
                step.microTiming = static_cast<juce::int8> ((stepIndex % 11) - 5);
                step.probability = static_cast<juce::uint8> ((stepIndex * 7) % 101);
                step.condition = static_cast<CycleCondition> (stepIndex % 9);
                step.ratchets = static_cast<juce::uint8> (1 + stepIndex % 8);
                step.ratchetRamp = static_cast<VelocityRamp> (stepIndex % 3);
                step.slide = stepIndex % 5 == 0;
                step.accent = stepIndex % 4 == 1;
                step.pitch = static_cast<juce::int8> ((stepIndex % 25) - 12);
                step.mod = static_cast<juce::uint8> ((stepIndex * 5) % 128);
            }
        }

        return pattern;
    }

    void testRoundTrip()
    {
        beginTest ("A pattern survives a save and load unchanged");

        const auto original = makeBusyPattern();

        juce::XmlElement state ("KINETIC_STATE");
        patternstate::writeToXml (original, 5, state);

        Pattern restored;
        int selectedLane = 0;

        expect (patternstate::readFromXml (state, restored, selectedLane));
        expect (patternsMatch (original, restored));
        expectEquals (selectedLane, 5);
    }

    void testDefaultStepsAreNotWritten()
    {
        beginTest ("Default steps cost almost nothing in the file");

        Pattern pattern;
        juce::XmlElement state ("KINETIC_STATE");
        patternstate::writeToXml (pattern, 0, state);

        const auto* element = state.getChildByName (patternstate::elementTag);
        expect (element != nullptr);

        if (element != nullptr)
        {
            const auto* lane = element->getChildByName ("lane");
            expect (lane != nullptr);

            if (lane != nullptr)
            {
                const auto* step = lane->getChildByName ("step");
                expect (step != nullptr);

                if (step != nullptr)
                {
                    expectEquals (static_cast<int> (step->getNumAttributes()), 1);   // just the index
                    expectEquals (step->getIntAttribute ("i"), 0);
                }
            }
        }
    }

    void testRestoreReplacesTheWholePattern()
    {
        beginTest ("Restoring replaces the pattern rather than merging into it");

        Pattern saved;
        auto& savedLane = saved.lanes[3];
        savedLane.settings.enabled = true;
        savedLane.settings.gateLength = 5;
        savedLane.steps[0].trigger = true;

        juce::XmlElement state ("KINETIC_STATE");
        patternstate::writeToXml (saved, 3, state);

        // The session currently holds something completely different.
        auto working = makeBusyPattern();
        int selectedLane = 0;

        expect (patternstate::readFromXml (state, working, selectedLane));
        expect (patternsMatch (saved, working));
        expect (! working.lanes[0].settings.enabled);   // leftovers cleared
        expectEquals (static_cast<int> (working.lanes[0].steps[7].velocity), 100);
        expect (working.lanes[3].settings.enabled);
    }

    void testMissingPatternIsIgnored()
    {
        beginTest ("A state without a pattern leaves the pattern untouched");

        auto working = makeBusyPattern();
        const auto before = working;
        int selectedLane = 2;

        juce::XmlElement state ("KINETIC_STATE");

        expect (! patternstate::readFromXml (state, working, selectedLane));
        expect (patternsMatch (before, working));
        expectEquals (selectedLane, 2);
    }

    void testSelectedLaneSurvives()
    {
        beginTest ("The selected lane survives and is clamped");

        const auto pattern = makeBusyPattern();
        int selectedLane = 0;

        juce::XmlElement state ("KINETIC_STATE");
        patternstate::writeToXml (pattern, maxLanes + 9, state);

        Pattern restored;
        expect (patternstate::readFromXml (state, restored, selectedLane));
        expectEquals (selectedLane, maxLanes - 1);
    }

    void testSurvivesTheTextForm()
    {
        beginTest ("The pattern survives being written and parsed as text");

        const auto original = makeBusyPattern();

        juce::XmlElement state ("KINETIC_STATE");
        patternstate::writeToXml (original, 1, state);

        const auto text = state.toString();
        auto parsed = juce::parseXML (text);
        expect (parsed != nullptr);

        if (parsed != nullptr)
        {
            Pattern restored;
            int selectedLane = 0;

            expect (patternstate::readFromXml (*parsed, restored, selectedLane));
            expect (patternsMatch (original, restored));
            expectEquals (selectedLane, 1);
        }
    }

    void testUnknownFormatIsRefused()
    {
        beginTest ("A pattern from an unknown format version is refused, not misread");

        juce::XmlElement state ("KINETIC_STATE");
        patternstate::writeToXml (makeBusyPattern(), 4, state);

        auto* element = state.getChildByName (patternstate::elementTag);
        expect (element != nullptr);

        if (element == nullptr)
            return;

        element->setAttribute ("format", patternstate::formatVersion + 1);

        // Whatever this session holds must survive a refused load untouched.
        auto working = makeBusyPattern();
        working.lanes[2].settings.gateLength = 9;
        working.globalRoot = 7;
        const auto before = working;
        int selectedLane = 2;

        expect (! patternstate::readFromXml (state, working, selectedLane));
        expect (patternsMatch (before, working));
        expectEquals (selectedLane, 2);
    }

    void testMissingFormatAttributeIsVersionOne()
    {
        beginTest ("A pattern without a format attribute is read as version 1");

        const auto original = makeBusyPattern();

        juce::XmlElement state ("KINETIC_STATE");
        patternstate::writeToXml (original, 6, state);

        auto* element = state.getChildByName (patternstate::elementTag);
        expect (element != nullptr);

        if (element == nullptr)
            return;

        element->removeAttribute ("format");

        Pattern restored;
        int selectedLane = 0;

        expect (patternstate::readFromXml (state, restored, selectedLane));
        expect (patternsMatch (original, restored));
        expectEquals (selectedLane, 6);
    }
};

static PatternStateTests patternStateTests;

} // namespace kinetic
