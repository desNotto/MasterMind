#include "ui/MasterView.h"
#include "ui/Locale.h"

namespace mm::ui
{
namespace
{
constexpr int barH = 58, caretH = 8, gap = 8;
} // namespace

// ---- LoudnessCompact -------------------------------------------------------------------------------------
void LoudnessCompact::paint (juce::Graphics& g)
{
    drawPanel (g, getLocalBounds().toFloat());
    paintPanelHeader (g, getLocalBounds(), tr ("Loudness"));
    g.setColour (col::text3);
    g.setFont (font (12.5f));
    g.drawText (tr ("open Eyes for history"), getLocalBounds().removeFromTop (32).reduced (12, 0), juce::Justification::centredRight, true);
    const auto& t = viewTap (ctx);
    auto r = getLocalBounds().withTrimmedTop (32).reduced (10, 6);
    const int cw = (r.getWidth() - 12) * 10 / 33;
    auto mainCell = r.removeFromLeft (r.getWidth() - 2 * cw - 12).toFloat();
    const auto cell = [&] (juce::Rectangle<float> c, const juce::String& label, const juce::String& value, juce::Colour colour, bool big)
    {
        g.setColour (col::panel2);
        g.fillRoundedRectangle (c, 6.0f);
        g.setColour (col::line);
        g.drawRoundedRectangle (c.reduced (0.5f), 6.0f, 1.0f);
        auto in = c.reduced (10.0f, 5.0f);
        if (big)
            in = in.withSizeKeepingCentre (in.getWidth(), 48.0f);
        g.setColour (col::text3);
        g.setFont (font (12.0f));
        g.drawText (label, in.removeFromTop (16.0f), juce::Justification::centredLeft, true);
        g.setColour (colour);
        g.setFont (font (big ? 26.0f : 16.0f, true));
        const int vw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (big ? 26.0f : 16.0f, true), value)) + 6;
        g.drawText (value, in.removeFromLeft ((float) vw), juce::Justification::centredLeft, false);
        if (big)
        {
            g.setColour (col::text3);
            g.setFont (font (12.0f));
            g.drawText ("LUFS", in, juce::Justification::centredLeft, false);
        }
    };
    cell (mainCell.reduced (0.0f, 1.0f), tr ("Integrated##loudness"), fmtLufs (t.integrated), juce::Colours::white, true);
    r.removeFromLeft (6);
    const float h = (r.getHeight() - 6.0f) / 2.0f;
    const double ceiling = ctx.param ("lim_ceiling");
    const auto tpCol = t.truePeak <= ceiling ? col::ok : (t.truePeak <= ceiling + 0.5 ? col::warn : col::bad);
    const auto plrCol = t.plr >= 8 ? col::text : (t.plr >= 6 ? col::warn : col::bad);
    const auto c1 = r.removeFromLeft (cw).toFloat();
    r.removeFromLeft (6);
    const auto c2 = r.toFloat();
    cell (c1.withHeight (h), tr ("Short-term"), fmtLufs (t.shortTerm), col::text, false);
    cell (c2.withHeight (h), "True peak", fmtLufs (t.truePeak), tpCol, false);
    cell (c1.withTrimmedTop (h + 6.0f), "PLR", juce::String (t.plr, 1), plrCol, false);
    cell (c2.withTrimmedTop (h + 6.0f), "LRA", juce::String (t.lra, 1), col::text, false);
}

// ---- MasterView ------------------------------------------------------------------------------------------
MasterView::MasterView (Context& c) : ctx (c), spectrum (c, false), bar (c), tonal (c, true), loudness (c), checks (c, false, true)
{
    for (juce::Component* comp : { (juce::Component*) &spectrum, (juce::Component*) &bar, (juce::Component*) &tonal, (juce::Component*) &loudness, (juce::Component*) &checks })
        addAndMakeVisible (comp);
    addChildComponent (editorView);
    editorView.setScrollBarsShown (true, false);
    for (int i = 0; i < 10; ++i)
    {
        editors.push_back (createModuleEditor (ctx, i));
        addChildComponent (*editors.back());
    }
}

MasterView::~MasterView()
{
    editorView.setViewedComponent (nullptr, false);
}

void MasterView::setMode (Mode m)
{
    if (m == mode)
        return;
    mode = m;
    bar.setCompact (m == Mode::compact);
    for (auto& e : editors)
        e->setCompact (m == Mode::compact);
    resized();
}

void MasterView::selectModule (int index)
{
    ctx.selectedModule = juce::jlimit (0, 9, index);
    update();
}

void MasterView::placeEditor (juce::Rectangle<int> area)
{
    const int sel = juce::jlimit (0, 9, ctx.selectedModule);
    auto& ed = *editors[(size_t) sel];
    for (size_t i = 0; i < editors.size(); ++i)
        if ((int) i != sel)
            editors[i]->setVisible (false);
    const int need = ed.naturalHeight (area.getWidth());
    if (need <= area.getHeight())
    {
        if (editorView.getViewedComponent() == &ed)
            editorView.setViewedComponent (nullptr, false);
        if (ed.getParentComponent() != this)
            addChildComponent (ed);
        editorView.setVisible (false);
        ed.setBounds (area);
        ed.setVisible (true);
    }
    else
    {
        // smaller windows: the editor scrolls (medium and compact layout modes)
        editorView.setBounds (area);
        editorView.setViewedComponent (&ed, false);
        ed.setBounds (0, 0, area.getWidth() - editorView.getScrollBarThickness(), need);
        ed.setVisible (true);
        editorView.setVisible (true);
    }
    shownModule = sel;
}

void MasterView::resized()
{
    auto r = getLocalBounds();
    if (mode == Mode::compact)
    {
        spectrum.setVisible (false);
        tonal.setVisible (false);
        loudness.setVisible (false);
        checks.setVisible (false);
        bar.setBounds (r.removeFromTop (44));
        r.removeFromTop (6);
        caretArea = {};
        placeEditor (r);
        return;
    }
    spectrum.setVisible (true);
    const bool column = mode == Mode::full;
    tonal.setVisible (column);
    loudness.setVisible (column);
    checks.setVisible (column);
    if (column)
    {
        const int rw = juce::jlimit (300, 392, r.getWidth() * 31 / 100);
        auto right = r.removeFromRight (rw);
        r.removeFromRight (gap);
        tonal.setBounds (right.removeFromTop (tonal.preferredHeight()));
        right.removeFromTop (gap);
        loudness.setBounds (right.removeFromTop (140));
        right.removeFromTop (gap);
        checks.setBounds (right);
    }
    int need = 0;
    for (auto& e : editors)
        need = std::max (need, e->naturalHeight (r.getWidth()));
    const int specH = juce::jlimit (150, 280, r.getHeight() - gap - barH - caretH - need);
    spectrum.setBounds (r.removeFromTop (specH));
    r.removeFromTop (gap);
    bar.setBounds (r.removeFromTop (barH));
    caretArea = r.removeFromTop (caretH);
    placeEditor (r);
    repaint();
}

void MasterView::paint (juce::Graphics& g)
{
    if (caretArea.isEmpty())
        return;
    // caret under the selected tile
    const float w = (bar.getWidth() - 6.0f * 9.0f) / 10.0f;
    const float cx = bar.getX() + ctx.selectedModule * (w + 6.0f) + w / 2.0f;
    juce::Path p;
    p.addTriangle (cx - 7.0f, (float) caretArea.getY(), cx + 7.0f, (float) caretArea.getY(), cx, (float) caretArea.getY() + 7.0f);
    g.setColour (col::orange);
    g.fillPath (p);
}

void MasterView::update()
{
    if (ctx.selectedModule != shownModule)
    {
        resized();
        repaint();
    }
    spectrum.update();
    bar.update();
    if (shownModule >= 0)
        editors[(size_t) shownModule]->update();
    if (tonal.isVisible())
    {
        tonal.repaint();
        loudness.repaint();
        checks.repaint();
    }
}
} // namespace mm::ui
