#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

//==============================================================================
// A slot-strip button (numbered 1-8). Behaves like a normal TextButton on
// left-click (select / audition); right-click invokes onRightClick, used to
// mute the whole slot in one gesture - so a slot holding several slices can be
// disabled without garbage-flagging each slice individually.
//
// When muted, the button is drawn greyed with a diagonal strike (a "disabled"
// look), regardless of how many slices the slot holds.
class SlotButton : public juce::TextButton
{
public:
    SlotButton() = default;

    // Invoked on right mouse-button press.
    std::function<void()> onRightClick;

    void setMuted (bool shouldBeMuted)
    {
        if (muted == shouldBeMuted) return;
        muted = shouldBeMuted;
        repaint();
    }
    bool isMuted() const noexcept { return muted; }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isRightButtonDown())
        {
            if (onRightClick) onRightClick();
            return;                         // don't begin a left-style press
        }
        juce::TextButton::mouseDown (e);
    }

    void paintButton (juce::Graphics& g, bool shouldDrawHighlighted,
                      bool shouldDrawDown) override
    {
        juce::TextButton::paintButton (g, shouldDrawHighlighted, shouldDrawDown);
        if (! muted) return;

        auto b = getLocalBounds().toFloat();
        // Grey wash to read as "disabled".
        g.setColour (juce::Colour (0xff1b1b1f).withAlpha (0.55f));
        g.fillRoundedRectangle (b, 3.0f);
        // Diagonal strike, bottom-left -> top-right.
        g.setColour (juce::Colour (0xff9a9aa6));
        g.drawLine (b.getX() + 2.0f, b.getBottom() - 2.0f,
                    b.getRight() - 2.0f, b.getY() + 2.0f, 1.5f);
    }

private:
    bool muted { false };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlotButton)
};
