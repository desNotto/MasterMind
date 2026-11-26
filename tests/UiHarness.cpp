// Layout test harness (spec section 10): renders the editor at several sizes, fails if any
// control leaves the window or overlaps a neighbouring control, and writes PNG snapshots.
#include "PluginProcessor.h"
#include "ui/Locale.h"
#include "ui/PluginEditor.h"
#include "ui/Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

namespace
{
bool isControl (juce::Component* c)
{
    return dynamic_cast<mm::ui::Knob*> (c) != nullptr || dynamic_cast<juce::Button*> (c) != nullptr || dynamic_cast<juce::ComboBox*> (c) != nullptr
        || dynamic_cast<mm::ui::Segmented*> (c) != nullptr || dynamic_cast<mm::ui::Led*> (c) != nullptr;
}

bool insideViewport (juce::Component* c)
{
    for (auto* p = c->getParentComponent(); p != nullptr; p = p->getParentComponent())
        if (dynamic_cast<juce::Viewport*> (p) != nullptr)
            return true;
    return false;
}

void collect (juce::Component* c, std::vector<juce::Component*>& out)
{
    if (! c->isVisible())
        return;
    if (isControl (c))
        out.push_back (c);
    for (auto* child : c->getChildren())
        collect (child, out);
}

int checkLayout (juce::Component& root, const juce::String& label)
{
    std::vector<juce::Component*> controls;
    collect (&root, controls);
    int failures = 0;
    const auto bounds = root.getLocalBounds();
    for (size_t i = 0; i < controls.size(); ++i)
    {
        auto* a = controls[i];
        if (insideViewport (a) || dynamic_cast<mm::ui::ModuleDetail*> (a->getParentComponent()) != nullptr)
            continue;
        const auto ra = root.getLocalArea (a, a->getLocalBounds());
        if (ra.isEmpty() || ! bounds.contains (ra))
        {
            std::printf ("  [%s] outside the window: %s %s\n", label.toRawUTF8(), a->getName().toRawUTF8(), ra.toString().toRawUTF8());
            ++failures;
        }
        for (size_t j = i + 1; j < controls.size(); ++j)
        {
            auto* b = controls[j];
            if (insideViewport (b) || a->isParentOf (b) || b->isParentOf (a))
                continue;
            const auto rb = root.getLocalArea (b, b->getLocalBounds());
            if (ra.reduced (1).intersects (rb.reduced (1)))
            {
                auto describe = [] (juce::Component* c)
                {
                    if (auto* btn = dynamic_cast<juce::Button*> (c))
                        return "button '" + btn->getButtonText() + "'";
                    if (auto* k = dynamic_cast<mm::ui::Knob*> (c))
                        return "knob " + k->getParamId();
                    return juce::String (typeid (*c).name());
                };
                std::printf ("  [%s] overlap: %s %s and %s %s\n", label.toRawUTF8(), describe (a).toRawUTF8(), ra.toString().toRawUTF8(),
                             describe (b).toRawUTF8(), rb.toString().toRawUTF8());
                ++failures;
            }
        }
    }
    return failures;
}

void feedAudio (MasterMindProcessor& p, double seconds)
{
    const double fs = 48000.0;
    const int block = 512;
    juce::AudioBuffer<float> b (2, block);
    juce::MidiBuffer midi;
    juce::Random r (5);
    double phase = 0.0, b0 = 0, b1 = 0, b2 = 0;
    for (int n = 0; n < (int) (seconds * fs / block); ++n)
    {
        for (int i = 0; i < block; ++i)
        {
            const double w = r.nextDouble() * 2.0 - 1.0;
            b0 = 0.99765 * b0 + w * 0.0990460;
            b1 = 0.96300 * b1 + w * 0.2965164;
            b2 = 0.57000 * b2 + w * 1.0526913;
            const double pink = (b0 + b1 + b2 + w * 0.1848) * 0.06;
            phase += 2.0 * juce::MathConstants<double>::pi * 46.25 / fs;
            const double kick = 0.35 * std::sin (phase);
            b.setSample (0, i, (float) (pink + kick));
            b.setSample (1, i, (float) (pink * 0.8 + kick));
        }
        p.processBlock (b, midi);
        if (n % 20 == 0)
            juce::Thread::sleep (1);
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File (juce::String (argv[1])) : juce::File::getCurrentWorkingDirectory().getChildFile ("ui-snapshots");
    outDir.createDirectory();

    // like pluginval's Editor test: open and close the editor before any audio was prepared
    {
        MasterMindProcessor fresh;
        for (int i = 0; i < 2; ++i)
        {
            std::unique_ptr<juce::AudioProcessorEditor> e (fresh.createEditor());
            e->setSize (1280, 800);
            for (int k = 0; k < 10; ++k)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (35);
                dynamic_cast<MasterMindEditor*> (e.get())->forceUpdate();
            }
            for (const char* tab : { "Eyes", "Low-End", "Stereo", "Spectrogram", "Album", "Master" })
            {
                dynamic_cast<MasterMindEditor*> (e.get())->showTab (tab);
                dynamic_cast<MasterMindEditor*> (e.get())->forceUpdate();
                (void) e->createComponentSnapshot (e->getLocalBounds(), true, 0.5f);
            }
        }
        std::printf ("unprepared editor: ok\n");
    }

    MasterMindProcessor proc;
    proc.setPlayConfigDetails (2, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    // a realistic chain so meters show something
    for (auto [id, v] : std::initializer_list<std::pair<const char*, double>> { { "eq_on", 1 }, { "eq_b3_on", 1 }, { "eq_b3_freq", 280 }, { "eq_b3_gain", -2 },
                                                                               { "eq_b5_on", 1 }, { "eq_b5_freq", 4800 }, { "eq_b5_gain", -2.5f }, { "eq_b5_q", 2 },
                                                                               { "eq_b5_dyn", 1 }, { "lem_on", 1 }, { "bus_on", 1 }, { "clip_on", 1 }, { "lim_on", 1 },
                                                                               { "lim_drive", 4 }, { "hpf_on", 1 } })
        if (auto* p = proc.getValueTreeState().getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
    feedAudio (proc, 6.0);
    for (int i = 0; i < 40; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    }

    auto editor = std::unique_ptr<MasterMindEditor> (dynamic_cast<MasterMindEditor*> (proc.createEditor()));
    struct Shot
    {
        int w, h;
        const char* tab;
        bool monitor;
    };
    const Shot shots[] = { { 1280, 800, "Master", false }, { 1280, 800, "Eyes", false },   { 1536, 960, "Master", true },     { 1536, 960, "Eyes", false },
                           { 640, 400, "Overview", false }, { 640, 400, "Overview", true }, { 1280, 800, "Low-End", false },   { 1280, 800, "Stereo", false },
                           { 1280, 800, "Spectrogram", false }, { 1280, 800, "Album", false }, { 960, 600, "Master", false }, { 960, 600, "Eyes", false },
                           { 480, 300, "Overview", false }, { 720, 460, "Master", false }, { 1000, 640, "Master", false }, { 3840, 2160, "Master", false } };
    int failures = 0;
    for (const auto& s : shots)
    {
        proc.getMonitoring().listenMode = s.monitor ? 1 : 0;
        feedAudio (proc, 0.5);
        editor->setSize (s.w, s.h);
        editor->showTab (s.tab);
        for (int i = 0; i < 6; ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            editor->forceUpdate();
        }
        const juce::String label = juce::String (s.w) + "x" + juce::String (s.h) + " " + s.tab + (s.monitor ? " monitor" : "");
        failures += checkLayout (editor->getRoot(), label);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile (juce::String (s.w) + "x" + juce::String (s.h) + "-" + juce::String (s.tab).toLowerCase() + (s.monitor ? "-monitor" : "") + ".png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (image, os);
        std::printf ("%s: %s\n", label.toRawUTF8(), file.getFullPathName().toRawUTF8());
    }
    // Russian interface strings
    mm::ui::setLanguage (mm::ui::Language::russian);
    for (const char* tab : { "Eyes", "Spectrogram" })
    {
        editor->setSize (1280, 800);
        editor->showTab (tab);
        for (int i = 0; i < 4; ++i)
        {
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
            editor->forceUpdate();
        }
        failures += checkLayout (editor->getRoot(), juce::String ("ru ") + tab);
        auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        const auto file = outDir.getChildFile ("1280x800-" + juce::String (tab).toLowerCase() + "-ru.png");
        file.deleteFile();
        juce::FileOutputStream os (file);
        juce::PNGImageFormat().writeImageToStream (image, os);
    }
    mm::ui::setLanguage (mm::ui::Language::english);
    editor.reset();
    proc.releaseResources();
    std::printf (failures == 0 ? "LAYOUT OK\n" : "LAYOUT FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
