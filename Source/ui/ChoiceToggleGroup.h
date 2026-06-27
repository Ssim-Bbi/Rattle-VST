#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cmath>

//==============================================================================
// A small segmented toggle-button group bound to an AudioParameterChoice.
// Renders N buttons in a row; the active one is amber. Drop-in replacement for
// a ComboBox on 2–3 option enum parameters. Host automation still works because
// it drives the parameter through a juce::ParameterAttachment.
//==============================================================================
class ChoiceToggleGroup : public juce::Component
{
public:
    ChoiceToggleGroup() = default;

    void attach (juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& paramID,
                 const juce::StringArray& labels)
    {
        param = apvts.getParameter (paramID);
        jassert (param != nullptr);

        buttons.clear();
        for (int i = 0; i < labels.size(); ++i)
        {
            auto* b = new juce::TextButton (labels[i]);
            b->setClickingTogglesState (false);
            b->setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff2e2e34));
            b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb86c00));
            b->setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff9a9aa6));
            b->setColour (juce::TextButton::textColourOnId,   juce::Colours::white);

            int edges = 0;
            if (i > 0)                 edges |= juce::Button::ConnectedOnLeft;
            if (i < labels.size() - 1) edges |= juce::Button::ConnectedOnRight;
            b->setConnectedEdges (edges);

            b->onClick = [this, i] { setIndex (i, true); };
            addAndMakeVisible (b);
            buttons.add (b);
        }

        if (param != nullptr)
        {
            attachment = std::make_unique<juce::ParameterAttachment> (
                *param, [this] (float v) { setIndex ((int) std::lround (v), false); });
            attachment->sendInitialUpdate();
        }
    }

    void resized() override
    {
        const int n = buttons.size();
        if (n == 0) return;
        auto r = getLocalBounds();
        const int w = r.getWidth() / n;
        for (int i = 0; i < n; ++i)
            buttons[i]->setBounds (i < n - 1 ? r.removeFromLeft (w) : r);
    }

private:
    void setIndex (int idx, bool fromUser)
    {
        for (int i = 0; i < buttons.size(); ++i)
            buttons[i]->setToggleState (i == idx, juce::dontSendNotification);

        if (fromUser && attachment != nullptr)
            attachment->setValueAsCompleteGesture ((float) idx);
    }

    juce::OwnedArray<juce::TextButton>         buttons;
    juce::RangedAudioParameter*                param { nullptr };
    std::unique_ptr<juce::ParameterAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChoiceToggleGroup)
};
