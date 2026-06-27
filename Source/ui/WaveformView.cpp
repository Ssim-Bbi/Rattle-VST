#include "WaveformView.h"

void WaveformView::clear()
{
    peaks.clear();
    markerNorm.clear();
    sliceGarbage.clear();
    slotMuted = false;
    repaint();
}

void WaveformView::setBuffer (const juce::AudioBuffer<float>* buf, double /*sampleRate*/)
{
    peaks.clear();

    if (buf != nullptr && buf->getNumSamples() > 0)
    {
        const int numSmp = buf->getNumSamples();
        const int numCh  = buf->getNumChannels();

        peaks.resize ((size_t) peakResolution * 2, 0.0f);

        for (int x = 0; x < peakResolution; ++x)
        {
            const int s0 = (int)((double) x       / peakResolution * numSmp);
            const int s1 = juce::jmin ((int)((double)(x + 1) / peakResolution * numSmp), numSmp);

            float mn = 0.0f, mx = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float* d = buf->getReadPointer (ch);
                for (int s = s0; s < s1; ++s)
                {
                    if (d[s] < mn) mn = d[s];
                    if (d[s] > mx) mx = d[s];
                }
            }
            peaks[(size_t) x * 2]     = mn;
            peaks[(size_t) x * 2 + 1] = mx;
        }
    }

    repaint();
}

void WaveformView::setMarkers (const std::vector<float>& normPositions)
{
    markerNorm = normPositions;
    repaint();
}

void WaveformView::setSliceGarbage (const std::vector<bool>& g)
{
    sliceGarbage = g;
    repaint();
}

void WaveformView::setSlotMuted (bool muted)
{
    if (slotMuted == muted) return;
    slotMuted = muted;
    repaint();
}

int WaveformView::regionAt (float normX) const
{
    const int n = (int) markerNorm.size();
    if (n == 0) return -1;
    if (normX < markerNorm[0]) return -1; // head region before the first marker is unplayed
    for (int k = 0; k < n; ++k)
    {
        const float lo = markerNorm[(size_t) k];
        const float hi = (k + 1 < n) ? markerNorm[(size_t)(k + 1)] : 1.0f;
        if (normX >= lo && normX < hi) return k;
    }
    return n - 1;
}

void WaveformView::setSampleName (const juce::String& name)
{
    if (sampleName == name) return;
    sampleName = name;
    repaint();
}

//==============================================================================
void WaveformView::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds();

    g.setColour (juce::Colour (0xff1e1e26));
    g.fillRoundedRectangle (b.toFloat(), 4.0f);

    g.setColour (juce::Colour (0xff3a3a46));
    g.drawRoundedRectangle (b.toFloat().reduced (0.5f), 4.0f, 1.0f);

    if (peaks.empty())
    {
        g.setColour (juce::Colour (0xff4a4a5a));
        g.setFont   (juce::FontOptions (13.0f));
        g.drawText  ("No sample loaded — click Load", b, juce::Justification::centred);
        return;
    }

    const int   w     = b.getWidth();
    const float midY  = (float) b.getCentreY();
    const float halfH = (float) b.getHeight() * 0.44f;

    g.setColour (juce::Colour (0xff5e5ef0));

    for (int x = 0; x < w; ++x)
    {
        const int   idx = (int)((double) x / w * peakResolution);
        const float mn  = peaks[(size_t) idx * 2];
        const float mx  = peaks[(size_t) idx * 2 + 1];
        g.drawVerticalLine (b.getX() + x,
                            midY - mx * halfH,
                            midY - mn * halfH + 1.0f);
    }

    // ---- Garbage / mute overlays (gray wash + big X) --------------------
    auto drawXOverlay = [&g] (juce::Rectangle<float> r)
    {
        g.setColour (juce::Colour (0xaa181820));
        g.fillRect (r);
        g.setColour (juce::Colour (0xffd24b4b).withAlpha (0.9f));
        const float p = 4.0f;
        g.drawLine (r.getX() + p, r.getY() + p, r.getRight() - p, r.getBottom() - p, 2.0f);
        g.drawLine (r.getX() + p, r.getBottom() - p, r.getRight() - p, r.getY() + p, 2.0f);
    };

    for (int k = 0; k < (int) markerNorm.size(); ++k)
    {
        if (k >= (int) sliceGarbage.size() || ! sliceGarbage[(size_t) k]) continue;
        const float lo = (float) b.getX() + markerNorm[(size_t) k] * (float) w;
        const float hi = (float) b.getX() + ((k + 1 < (int) markerNorm.size())
                              ? markerNorm[(size_t)(k + 1)] : 1.0f) * (float) w;
        drawXOverlay ({ lo, (float) b.getY(), hi - lo, (float) b.getHeight() });
    }

    // ---- Slice marker lines (amber vertical) ----------------------------
    for (int k = 0; k < (int) markerNorm.size(); ++k)
    {
        const float px = (float) b.getX() + markerNorm[k] * (float) w;
        const bool  isDragged = (k == draggedIdx);
        g.setColour (isDragged ? juce::Colour (0xffffaa00)
                               : juce::Colour (0xffcc8800).withAlpha (0.9f));
        g.drawLine (px, (float) b.getY(), px, (float) b.getBottom(), isDragged ? 2.0f : 1.5f);

        // Small triangle handle at top.
        juce::Path tri;
        tri.addTriangle (px - 4.f, (float) b.getY(),
                         px + 4.f, (float) b.getY(),
                         px,       (float) b.getY() + 7.f);
        g.fillPath (tri);
    }

    // ---- Whole-slot mute overlay (automatable per-slot mute) ------------
    if (slotMuted)
        drawXOverlay (b.toFloat());

    // ---- Sample name (top-right) ----------------------------------------
    if (sampleName.isNotEmpty())
    {
        g.setFont (juce::FontOptions (12.0f));
        const int tw = juce::jmin (b.getWidth() - 16, 220);
        const juce::Rectangle<int> nameArea (b.getRight() - 8 - tw, b.getY() + 5, tw, 15);
        g.setColour (juce::Colour (0xaa14141a)); // subtle backing for legibility over the wave
        g.fillRoundedRectangle (nameArea.toFloat().expanded (3.0f, 1.0f), 3.0f);
        g.setColour (juce::Colour (0xffb0b0bc));
        g.drawText (sampleName, nameArea, juce::Justification::centredRight, true);
    }
}

//==============================================================================
void WaveformView::mouseDown (const juce::MouseEvent& e)
{
    draggedIdx  = -1;
    didDrag     = false;
    dragOutside = false;

    const float w = (float) getWidth();
    if (w < 1.0f || peaks.empty()) return;

    const float normX = (float) e.x / w;

    // Right-click: toggle garbage on the slice under the cursor (sliced slot),
    // or mute the whole slot (unsliced sample).
    if (e.mods.isRightButtonDown())
    {
        if (! markerNorm.empty())
        {
            const int region = regionAt (normX);
            if (region >= 0 && onSliceGarbageToggled)
                onSliceGarbageToggled (region);
        }
        else if (onSlotMuteToggled)
        {
            onSlotMuteToggled();
        }
        return;
    }

    // Left-click: pick the nearest marker to drag.
    float minDist = (float) dragThreshPx / w;
    for (int k = 0; k < (int) markerNorm.size(); ++k)
    {
        const float d = std::fabs (markerNorm[k] - normX);
        if (d < minDist) { minDist = d; draggedIdx = k; }
    }
    repaint();
}

void WaveformView::mouseDrag (const juce::MouseEvent& e)
{
    if (draggedIdx < 0 || draggedIdx >= (int) markerNorm.size()) return;

    const float w = (float) getWidth();
    if (w < 1.0f) return;

    didDrag     = true;
    dragOutside = ! getLocalBounds().contains (e.getPosition());

    const float targetPos = juce::jlimit (0.0f, 1.0f, (float) e.x / w);
    markerNorm[draggedIdx] = targetPos;
    std::sort (markerNorm.begin(), markerNorm.end());

    // draggedIdx may have shifted after the sort — re-find it by proximity.
    float minDist = 2.0f;
    for (int k = 0; k < (int) markerNorm.size(); ++k)
    {
        const float d = std::fabs (markerNorm[k] - targetPos);
        if (d < minDist) { minDist = d; draggedIdx = k; }
    }
    repaint();
}

void WaveformView::mouseUp (const juce::MouseEvent&)
{
    if (draggedIdx >= 0 && draggedIdx < (int) markerNorm.size())
    {
        if (dragOutside)                       // dragged off the component → delete it
            markerNorm.erase (markerNorm.begin() + draggedIdx);

        if ((dragOutside || didDrag) && onMarkersChanged)
            onMarkersChanged (markerNorm);
    }

    draggedIdx  = -1;
    didDrag     = false;
    dragOutside = false;
    repaint();
}

void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (peaks.empty()) return; // no sample loaded

    const float w = (float) getWidth();
    if (w < 1.0f) return;

    draggedIdx = -1; // cancel any drag the preceding mouseDown may have started

    const float normX   = juce::jlimit (0.0f, 1.0f, (float) e.x / w);
    const float minDist = (float) dragThreshPx / w;

    // On an existing marker → delete it; otherwise add a new marker.
    for (int k = 0; k < (int) markerNorm.size(); ++k)
        if (std::fabs (markerNorm[k] - normX) < minDist)
        {
            markerNorm.erase (markerNorm.begin() + k);
            if (onMarkersChanged) onMarkersChanged (markerNorm);
            repaint();
            return;
        }

    markerNorm.push_back (normX);
    std::sort (markerNorm.begin(), markerNorm.end());
    if (onMarkersChanged) onMarkersChanged (markerNorm);
    repaint();
}
