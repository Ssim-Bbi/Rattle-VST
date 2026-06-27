#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>

//==============================================================================
// Tiny indicator dot: a dim base ring that lights amber as level → 1. Sits over
// a slot button's corner and is transparent to mouse clicks so the button below
// still receives them.
//==============================================================================
class LedDot : public juce::Component
{
public:
    LedDot() { setInterceptsMouseClicks (false, false); }

    void setLevel (float l)
    {
        l = juce::jlimit (0.0f, 1.0f, l);
        if (std::abs (l - level) < 0.01f) return;
        level = l;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (juce::Colour (0xff3a3a44));
        g.fillEllipse (r);
        if (level > 0.01f)
        {
            g.setColour (juce::Colour (0xffffaa00).withAlpha (level));
            g.fillEllipse (r);
        }
    }

private:
    float level { 0.0f };
};
