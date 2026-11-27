#include "ui/TopBar.h"

#include "ui/TooltipText.h"

namespace mm::ui
{
namespace
{
juce::String ru (const char* s) { return juce::String (juce::CharPointer_UTF8 (s)); }
} // namespace

/** Reference slot button 1..6. */
class TopBar::RefButton final : public juce::Component
{
public:
    RefButton (TopBar& o, int i) : owner (o), index (i) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

    void paint (juce::Graphics& g) override
    {
        const auto& slot = owner.ctx.refs.slot (index);
        const bool selected = owner.ctx.refs.selected() == index;
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        if (selected && slot.state == reference::ReferenceLibrary::State::ready)
        {
            g.setColour (col::magenta);
            g.fillRoundedRectangle (r, 5.0f);
        }
        else
        {
            g.setColour (slot.state == reference::ReferenceLibrary::State::empty ? col::sunk : col::panel2);
            g.fillRoundedRectangle (r, 5.0f);
            if (slot.state == reference::ReferenceLibrary::State::analysing)
            {
                g.setColour (col::blue.withAlpha (0.35f));
                g.fillRoundedRectangle (r.withWidth (r.getWidth() * slot.progress), 5.0f);
            }
            g.setColour (slot.state == reference::ReferenceLibrary::State::failed ? col::bad : col::line2);
            if (slot.state == reference::ReferenceLibrary::State::empty)
            {
                juce::Path p;
                p.addRoundedRectangle (r, 5.0f);
                const float dash[] = { 3.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (p, p, dash, 2);
                g.fillPath (p);
            }
            else
                g.drawRoundedRectangle (r, 5.0f, 1.0f);
        }
        g.setColour (selected ? juce::Colours::white : (slot.state == reference::ReferenceLibrary::State::empty ? col::text3 : col::text));
        g.setFont (font (13.0f, selected));
        g.drawText (juce::String (index + 1), getLocalBounds(), juce::Justification::centred);
        if (slot.state == reference::ReferenceLibrary::State::ready && slot.inSet && ! selected)
        {
            g.setColour (col::info);
            g.fillEllipse ((float) getWidth() - 6.0f, 3.0f, 3.0f, 3.0f);
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            owner.showRefMenu (index);
        else if (owner.ctx.refs.slot (index).state == reference::ReferenceLibrary::State::empty)
            owner.loadRefFile (index);
        else
            owner.selectRef (index);
    }

    TopBar& owner;
    int index;
};

TopBar::TopBar (Context& c) : ctx (c)
{
    for (auto* b : { &prev, &next, &presetBox, &save, &saveAsBtn, &bypass, &match, &sizeBtn })
        addAndMakeVisible (*b);
    addAndMakeVisible (ab);
    for (int i = 0; i < reference::ReferenceLibrary::numSlots; ++i)
    {
        refButtons.push_back (std::make_unique<RefButton> (*this, i));
        addAndMakeVisible (*refButtons.back());
        setTip (*refButtons.back(), "c:ref:" + juce::String (i + 1));
    }
    setTip (prev, "c:pre.prev");
    setTip (next, "c:pre.next");
    setTip (presetBox, "c:pre.list");
    setTip (save, "c:pre.save");
    setTip (saveAsBtn, "c:pre.saveas");
    setTip (bypass, "c:bypass");
    setTip (match, "c:match");
    setTip (ab, "c:ab");
    setTip (sizeBtn, "c:size");

    prev.onClick = [this] { juce::String e; ctx.presets.loadNext (-1, e); };
    next.onClick = [this] { juce::String e; ctx.presets.loadNext (1, e); };
    presetBox.onClick = [this] { showPresetMenu(); };
    save.onClick = [this]
    {
        juce::String err;
        if (ctx.presets.save (err) == presets::PresetManager::Result::factoryReadOnly)
            saveAs(); // Save on a factory preset opens Save As (spec 9)
        else if (err.isNotEmpty() && ctx.notify)
            ctx.notify (err);
    };
    saveAsBtn.onClick = [this] { saveAs(); };
    bypass.onClick = [this] { ctx.monitoring.chainBypass = bypass.getToggleState(); };
    match.setToggleState (true, juce::dontSendNotification);
    match.onClick = [this] { ctx.monitoring.matchOn = match.getToggleState(); };
    ab.onChange = [this] (int i)
    {
        if (i == 1 && ctx.refs.selected() < 0)
        {
            for (int s = 0; s < reference::ReferenceLibrary::numSlots; ++s)
                if (ctx.refs.slot (s).state == reference::ReferenceLibrary::State::ready)
                {
                    ctx.refs.select (s);
                    break;
                }
        }
        ctx.monitoring.referenceActive = (i == 1 && ctx.refs.selected() >= 0);
        ctx.monitoring.refPlaying = ctx.monitoring.referenceActive.load();
        ab.setSelected (ctx.monitoring.referenceActive ? 1 : 0, false);
    };
    sizeBtn.onClick = [this] { if (onSizeButton) onSizeButton(); };

    ctx.presets.addChangeListener (this);
    ctx.refs.addChangeListener (this);
}

TopBar::~TopBar()
{
    ctx.presets.removeChangeListener (this);
    ctx.refs.removeChangeListener (this);
}

void TopBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    update();
    repaint();
}

void TopBar::setCompact (bool c)
{
    compact = c;
    for (auto* b : { &prev, &next, &save, &saveAsBtn, &match })
        b->setVisible (! compact);
    save.setVisible (! compact && ! medium);
    saveAsBtn.setVisible (! compact && ! medium);
    for (auto& r : refButtons)
        r->setVisible (! compact);
    ab.setVisible (! compact);
    resized();
}

void TopBar::update()
{
    auto name = ctx.presets.currentName();
    if (name.length() > 16 && ! compact)
        name = name.substring (0, 15) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6"));
    if (ctx.presets.isMissing())
        name += " (missing)";
    else if (ctx.presets.isModified())
        name += " *";
    presetBox.setButtonText (name);

    bypass.setToggleState (ctx.monitoring.chainBypass.load(), juce::dontSendNotification);
    ab.setSelected (ctx.monitoring.referenceActive.load() ? 1 : 0, false);
    for (auto& r : refButtons)
        r->repaint();
    repaint (meterArea);
    repaint (monGroup.expanded (4));
}

void TopBar::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    g.setColour (col::line);
    g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

    // logo
    g.setFont (font (compact ? 15.0f : 17.0f, true));
    if (compact)
    {
        g.setColour (col::text);
        g.drawText ("M", logoArea.withWidth (14), juce::Justification::centredLeft);
        g.setColour (col::orange);
        g.drawText ("M", logoArea.withTrimmedLeft (14), juce::Justification::centredLeft);
    }
    else
    {
        g.setColour (col::text);
        g.drawText ("Master", logoArea, juce::Justification::centredLeft);
        g.setColour (col::orange);
        g.drawText ("Mind", logoArea.withTrimmedLeft (juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (17.0f, true), "Master"))), juce::Justification::centredLeft);
    }

    const bool monitorOn = ctx.proc.monitorActive.load();
    for (auto r : { presetGroup, abGroup, refGroup, meterArea })
        if (! r.isEmpty())
        {
            g.setColour (col::panel);
            g.fillRoundedRectangle (r.toFloat(), 8.0f);
            g.setColour (col::line);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
        }
    if (! monGroup.isEmpty())
    {
        g.setColour (monitorOn ? col::mon : col::panel);
        g.fillRoundedRectangle (monGroup.toFloat(), 8.0f);
        g.setColour (monitorOn ? juce::Colour (0xffff6a70) : col::line);
        g.drawRoundedRectangle (monGroup.toFloat().reduced (0.5f), 8.0f, 1.0f);
        // safety badge (spec 6.0): only while reference B or a band listen plays (mockup v10)
        g.setColour (juce::Colours::white);
        g.setFont (font (13.0f, true));
        auto t = monGroup.reduced (10, 0);
        g.drawText ("MONITOR ON", t.removeFromLeft (92), juce::Justification::centredLeft);
        if (! compact)
        {
            juce::String what;
            if (ctx.monitoring.referenceActive.load())
                if (const auto* ref = ctx.monitoring.currentReferenceForUi())
                    what = "B: " + ref->name;
            if (what.isEmpty() && ctx.monitoring.bandSolo.load() >= 0)
                what = juce::String ("Listen: ") + analysis::tonalBandName (ctx.monitoring.bandSolo.load());
            g.setFont (font (12.5f));
            g.drawText (what, t.withTrimmedLeft (6), juce::Justification::centredLeft, true);
        }
    }

    // meters: S-T, TP, PLR (out tap)
    if (! meterArea.isEmpty())
    {
        const auto& o = ctx.out();
        const double ceiling = ctx.param ("lim_ceiling");
        const auto tpCol = o.truePeak <= ceiling ? col::ok : (o.truePeak <= ceiling + 0.5 ? col::warn : col::bad);
        const auto plrCol = o.plr >= 8.0 ? col::ok : (o.plr >= 6.0 ? col::warn : col::bad);
        auto r = meterArea.reduced (10, 0);
        const auto drawMeter = [&] (const juce::String& key, const juce::String& value, juce::Colour c, int w, const juce::String& unit = {})
        {
            auto cell = r.removeFromLeft (w);
            g.setFont (font (12.0f));
            g.setColour (col::text3);
            const int kw = key.isEmpty() ? 0 : juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (12.0f), key)) + 4;
            g.drawText (key, cell.removeFromLeft (kw), juce::Justification::centredLeft);
            g.setFont (font (compact ? 14.0f : 17.0f, true));
            g.setColour (c);
            const int vw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (font (compact ? 14.0f : 17.0f, true), value)) + 3;
            g.drawText (value, cell.removeFromLeft (vw), juce::Justification::centredLeft);
            if (unit.isNotEmpty())
            {
                g.setFont (font (12.0f));
                g.setColour (col::text3);
                g.drawText (unit, cell, juce::Justification::centredLeft);
            }
        };
        if (compact)
        {
            drawMeter ({}, fmtLufs (o.shortTerm), col::text, 72, "LUFS");
            drawMeter ({}, fmtLufs (o.truePeak), tpCol, 70, "dBTP");
        }
        else
        {
            drawMeter ("S-T", fmtLufs (o.shortTerm), col::text, 72);
            drawMeter ("TP", fmtLufs (o.truePeak), tpCol, 64);
            if (meterArea.getWidth() >= 230)
                drawMeter ("PLR", o.hasSignal ? juce::String (o.plr, 1) : juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")), o.hasSignal ? plrCol : col::text2, 64);
        }
    }
}

void TopBar::resized()
{
    auto r = getLocalBounds().reduced (compact ? 6 : 10, compact ? 6 : 8);
    const int h = r.getHeight();
    const int bh = compact ? 22 : 28;
    const auto centreY = [&] (juce::Rectangle<int> b) { return b.withSizeKeepingCentre (b.getWidth(), bh); };

    logoArea = r.removeFromLeft (compact ? 30 : 96);
    r.removeFromLeft (6);

    if (compact)
    {
        presetBox.setBounds (centreY (r.removeFromLeft (108)));
        r.removeFromLeft (6);
        bypass.setBounds (centreY (r.removeFromLeft (64)));
        r.removeFromLeft (6);
        monGroup = ctx.proc.monitorActive.load() ? r.removeFromLeft (112).withSizeKeepingCentre (112, h) : juce::Rectangle<int>();
        sizeBtn.setBounds (centreY (r.removeFromRight (64)));
        r.removeFromRight (6);
        meterArea = r.getWidth() >= 150 ? r.removeFromRight (std::min (r.getWidth(), 158)).withSizeKeepingCentre (std::min (r.getWidth(), 158), h) : juce::Rectangle<int>();
        presetGroup = abGroup = refGroup = {};
        return;
    }

    presetGroup = r.removeFromLeft (medium ? 4 + 24 + 2 + 24 + 4 + 116 + 4 : 4 + 24 + 2 + 24 + 4 + 116 + 4 + 46 + 4 + 64 + 4);
    {
        auto g = presetGroup.reduced (4, 0);
        prev.setBounds (centreY (g.removeFromLeft (24)));
        g.removeFromLeft (2);
        next.setBounds (centreY (g.removeFromLeft (24)));
        g.removeFromLeft (4);
        presetBox.setBounds (centreY (g.removeFromLeft (116)));
        g.removeFromLeft (4);
        if (! medium)
        {
            save.setBounds (centreY (g.removeFromLeft (46)));
            g.removeFromLeft (4);
            saveAsBtn.setBounds (centreY (g.removeFromLeft (64)));
        }
    }
    r.removeFromLeft (8);
    abGroup = r.removeFromLeft (4 + 60 + 4 + 56 + 4);
    {
        auto g = abGroup.reduced (4, 0);
        bypass.setBounds (centreY (g.removeFromLeft (60)));
        g.removeFromLeft (4);
        match.setBounds (centreY (g.removeFromLeft (56)));
    }
    r.removeFromLeft (8);
    refGroup = r.removeFromLeft (4 + 6 * 24 + 4 + 48 + 4);
    {
        auto g = refGroup.reduced (4, 0);
        for (auto& b : refButtons)
        {
            b->setBounds (centreY (g.removeFromLeft (22)));
            g.removeFromLeft (2);
        }
        g.removeFromLeft (4);
        ab.setBounds (centreY (g.removeFromLeft (48)));
    }
    r.removeFromLeft (8);
    monGroup = ctx.proc.monitorActive.load() ? r.removeFromLeft (std::min (230, std::max (0, r.getWidth() - 300))) : juce::Rectangle<int>();
    sizeBtn.setBounds (centreY (r.removeFromRight (54)));
    r.removeFromRight (8);
    // meters: all three, or S-T and TP when space is short
    meterArea = r.getWidth() >= 236 ? r.removeFromRight (232) : (r.getWidth() >= 160 ? r.removeFromRight (156) : juce::Rectangle<int>());
}

void TopBar::showPresetMenu()
{
    auto& pm = ctx.presets;
    const auto& list = pm.refresh();
    juce::PopupMenu menu;
    juce::String lastCategory;
    std::map<juce::String, juce::PopupMenu> userMenus;
    for (size_t i = 0; i < list.size(); ++i)
    {
        const auto& e = list[i];
        if (e.category != lastCategory && (e.factory || e.category == "User"))
        {
            menu.addSectionHeader (e.category);
            lastCategory = e.category;
        }
        const bool current = e.name == pm.currentName() && e.factory == pm.currentIsFactory();
        if (e.factory || e.category == "User")
            menu.addItem ((int) i + 1, e.name, true, current);
        else
            userMenus[e.category.fromFirstOccurrenceOf ("User/", false, false)].addItem ((int) i + 1, e.name, true, current);
    }
    for (auto& [cat, sub] : userMenus)
        menu.addSubMenu (cat, sub);
    menu.addSeparator();
    juce::PopupMenu manage;
    for (size_t i = 0; i < list.size(); ++i)
        if (! list[i].factory)
        {
            juce::PopupMenu item;
            item.addItem (10000 + (int) i, "Rename...");
            item.addItem (20000 + (int) i, "Delete...");
            manage.addSubMenu (list[i].name, item);
        }
    if (manage.getNumItems() > 0)
        menu.addSubMenu ("Rename / Delete", manage);
    menu.addItem (30004, "Save", ! pm.currentIsFactory());
    menu.addItem (30005, "Save As...");
    menu.addItem (30001, "Import...");
    menu.addItem (30002, "Export current...");
    menu.addItem (30003, "Open presets folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (presetBox), [this, list] (int r)
    {
        juce::String err;
        if (r >= 1 && r <= (int) list.size())
            ctx.presets.load (list[(size_t) r - 1], err);
        else if (r >= 10000 && r < 20000)
        {
            const auto e = list[(size_t) (r - 10000)];
            auto* w = new juce::AlertWindow ("Rename preset", "New name for \"" + e.name + "\"", juce::MessageBoxIconType::NoIcon);
            w->addTextEditor ("name", e.name);
            w->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w, e] (int res)
            {
                juce::String err2;
                if (res == 1 && ctx.presets.rename (e, w->getTextEditorContents ("name"), err2) != presets::PresetManager::Result::ok && ctx.notify)
                    ctx.notify (err2);
            }), true);
        }
        else if (r >= 20000 && r < 30000)
        {
            const auto e = list[(size_t) (r - 20000)];
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete preset",
                                                "Delete \"" + e.name + "\"? The file is removed.", "Delete", "Cancel", nullptr,
                                                juce::ModalCallbackFunction::create ([this, e] (int res)
                                                {
                                                    juce::String err2;
                                                    if (res == 1)
                                                        ctx.presets.remove (e, err2);
                                                }));
        }
        else if (r == 30001)
        {
            chooser = std::make_unique<juce::FileChooser> ("Import preset", juce::File(), "*.mbpreset");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                juce::String err2;
                if (fc.getResult().existsAsFile() && ctx.presets.importFile (fc.getResult(), err2) != presets::PresetManager::Result::ok && ctx.notify)
                    ctx.notify (err2);
            });
        }
        else if (r == 30002)
        {
            chooser = std::make_unique<juce::FileChooser> ("Export preset", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (ctx.presets.currentName() + ".mbpreset"), "*.mbpreset");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
            {
                juce::String err2;
                if (fc.getResult() != juce::File())
                    ctx.presets.exportTo (fc.getResult().withFileExtension ("mbpreset"), err2);
            });
        }
        else if (r == 30003)
            ctx.presets.getUserFolder().startAsProcess();
        else if (r == 30004)
            save.onClick();
        else if (r == 30005)
            saveAs();
        if (err.isNotEmpty() && ctx.notify)
            ctx.notify (err);
    });
}

void TopBar::saveAs()
{
    auto* w = new juce::AlertWindow ("Save As", "Name of the new preset (1 to 64 characters)", juce::MessageBoxIconType::NoIcon);
    w->addTextEditor ("name", ctx.presets.currentIsFactory() ? ctx.presets.currentName() + " (my)" : ctx.presets.currentName());
    w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int res)
    {
        if (res != 1)
            return;
        const auto name = w->getTextEditorContents ("name").trim();
        juce::String err;
        const auto r = ctx.presets.saveAs (name, false, err);
        if (r == presets::PresetManager::Result::exists)
        {
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Save As", "\"" + name + "\" exists. Overwrite it?", "Overwrite", "Cancel", nullptr,
                                                juce::ModalCallbackFunction::create ([this, name] (int res2)
                                                {
                                                    juce::String err2;
                                                    if (res2 == 1)
                                                        ctx.presets.saveAs (name, true, err2);
                                                }));
        }
        else if (r != presets::PresetManager::Result::ok && ctx.notify)
            ctx.notify (err);
    }), true);
}

void TopBar::selectRef (int slot)
{
    ctx.refs.select (slot);
    ctx.monitoring.referenceActive = true;
    ctx.monitoring.refPlaying = true;
    update();
}

void TopBar::loadRefFile (int slot)
{
    chooser = std::make_unique<juce::FileChooser> ("Load reference " + juce::String (slot + 1), juce::File(), "*.wav;*.flac;*.mp3;*.ogg;*.aif;*.aiff");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, slot] (const juce::FileChooser& fc)
    {
        if (fc.getResult().existsAsFile())
        {
            ctx.refs.load (slot, fc.getResult());
            ctx.proc.syncExtraState();
        }
    });
}

void TopBar::showRefMenu (int slot)
{
    const auto& s = ctx.refs.slot (slot);
    juce::PopupMenu menu;
    menu.addSectionHeader ("Reference " + juce::String (slot + 1) + (s.file != juce::File() ? ": " + s.file.getFileName() : juce::String()));
    if (s.state == reference::ReferenceLibrary::State::failed)
        menu.addItem (0, s.error, false);
    menu.addItem (1, "Load file...");
    menu.addItem (2, "Clear", s.state != reference::ReferenceLibrary::State::empty);
    menu.addItem (3, "In reference set (target and corridor)", s.state == reference::ReferenceLibrary::State::ready, s.inSet);
    menu.addSeparator();
    menu.addItem (4, ctx.monitoring.refPlaying ? "Pause" : "Play", s.state == reference::ReferenceLibrary::State::ready);
    menu.addItem (5, "Play from start", s.state == reference::ReferenceLibrary::State::ready);
    juce::PopupMenu matchMenu;
    matchMenu.addItem (10, "Auto: current section (short-term)", true, ctx.monitoring.refMatchMode == 0);
    matchMenu.addItem (11, "Auto: whole track (integrated)", true, ctx.monitoring.refMatchMode == 1);
    matchMenu.addItem (12, "Manual (0 dB)", true, ctx.monitoring.refMatchMode == 2);
    menu.addSubMenu ("Loudness matching", matchMenu);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (refButtons[(size_t) slot].get()), [this, slot] (int r)
    {
        auto& mon = ctx.monitoring;
        if (r == 1)
            loadRefFile (slot);
        else if (r == 2)
        {
            ctx.refs.clear (slot);
            mon.referenceActive = ctx.refs.selected() >= 0 && mon.referenceActive.load();
        }
        else if (r == 3)
            ctx.refs.setInSet (slot, ! ctx.refs.slot (slot).inSet);
        else if (r == 4)
        {
            if (ctx.refs.selected() != slot)
                selectRef (slot);
            else
                mon.refPlaying = ! mon.refPlaying.load();
        }
        else if (r == 5)
        {
            selectRef (slot);
            mon.seekRequest = 0.0;
        }
        else if (r >= 10 && r <= 12)
            mon.refMatchMode = r - 10;
        ctx.proc.syncExtraState();
        update();
    });
}

bool TopBar::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (juce::File (f).hasFileExtension ("wav;flac;mp3;ogg;aif;aiff;mbpreset"))
            return true;
    return false;
}

void TopBar::filesDropped (const juce::StringArray& files, int x, int y)
{
    int slot = -1;
    for (size_t i = 0; i < refButtons.size(); ++i)
        if (refButtons[i]->getBounds().expanded (2).contains (x, y))
            slot = (int) i;
    for (const auto& path : files)
    {
        const juce::File f (path);
        if (f.hasFileExtension ("mbpreset"))
        {
            juce::String err;
            if (ctx.presets.importFile (f, err) != presets::PresetManager::Result::ok && ctx.notify)
                ctx.notify (err);
            continue;
        }
        if (slot < 0) // first empty slot
            for (int i = 0; i < reference::ReferenceLibrary::numSlots; ++i)
                if (ctx.refs.slot (i).state == reference::ReferenceLibrary::State::empty)
                {
                    slot = i;
                    break;
                }
        if (slot >= 0)
        {
            ctx.refs.load (slot, f);
            slot = -1;
        }
    }
    ctx.proc.syncExtraState();
}
} // namespace mm::ui
