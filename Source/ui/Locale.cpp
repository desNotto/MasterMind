#include "ui/Locale.h"

namespace mm::ui
{
const std::map<juce::String, juce::String>& russianTable()
{
    static const std::map<juce::String, juce::String> t = []
    {
        std::map<juce::String, juce::String> m;
        const auto add = [&m] (const char* en, const char* ru) { m[juce::String (juce::CharPointer_UTF8 (en))] = juce::String (juce::CharPointer_UTF8 (ru)); };
        // headings
        add ("Spectrum", "Спектр");
        add ("Signal chain", "Цепочка обработки");
        add ("10 modules, fixed order: left to right, top to bottom", "10 модулей, порядок фиксирован: слева направо, сверху вниз");
        add ("Chain latency", "Задержка цепочки");
        add ("Tonal balance", "Тональный баланс");
        add ("vs target", "относительно цели");
        add ("Integrated##tonal", "Весь трек");
        add ("Last 10 s", "Последние 10 с");
        add ("Target: ", "Цель: ");
        add ("Loudness", "Громкость");
        add ("Integrated##loudness", "Интегральная");
        add ("Short-term", "Кратковременная");
        add ("Momentary", "Моментальная");
        add ("Short-term history", "История кратковременной громкости");
        add ("Sample peak", "Пик сэмплов");
        add ("Crest", "Крест-фактор");
        add ("Overs", "Перегрузки");
        add ("TP overs", "Перегрузки TP");
        add ("Checks", "Проверки");
        add ("independent checks, hover for advice", "независимые проверки, наведите курсор для совета");
        add ("Low end", "Низкие частоты");
        add ("Stereo##panel", "Стерео");
        add ("Correlation", "Корреляция");
        add ("Band", "Полоса");
        add ("Width", "Ширина");
        add ("Mono loss", "Потеря в моно");
        add ("Peak asymmetry", "Асимметрия пиков");
        add ("Low band 20-150 Hz, last 2 s", "Низкая полоса 20–150 Hz, последние 2 с");
        add ("Open Low-End tab", "Открыть Low-End");
        add ("Low-band listen", "Прослушивание низа");
        add ("Crest per band (5.8)", "Крест-фактор по полосам");
        add ("Width and mono compatibility per band", "Ширина и совместимость с моно по полосам");
        add ("Spectrogram", "Спектрограмма");
        add ("mid signal, log frequency", "сигнал Mid, логарифмическая шкала");
        add ("Harshness", "Жёсткость");
        add ("Dynamics per band", "Динамика по полосам");
        add ("crest factor, dB", "крест-фактор, dB");
        add ("Overs and limiter damage", "Перегрузки и работа лимитера");
        add ("Album", "Альбом");
        add ("Spectra of all tracks", "Спектры всех треков");
        add ("Loudness in track order", "Громкость по порядку треков");
        add ("Play audio to measure", "Включите воспроизведение для измерения");
        add ("All six bands are within 1 dB of the target", "Все шесть полос в пределах 1 dB от цели");
        add ("vs reference set", "относительно референсов");
        add ("vs default target", "относительно цели по умолчанию");
        // check titles and details
        add ("Mono loss##check", "Потеря в моно");
        add ("Low-band corr.", "Корр. низа");
        add ("Harshness##check", "Жёсткость");
        add ("Integrated##check", "Интегральная");
        add ("Within target", "В норме");
        add ("Above target", "Выше цели");
        add ("Below target", "Ниже цели");
        add ("Safe on mono phones", "Безопасно в моно");
        add ("Phase problems in mono", "Проблемы фазы в моно");
        add ("Bass is mono-safe", "Низ безопасен в моно");
        add ("Bass is not mono-safe", "Низ небезопасен в моно");
        add ("Healthy dynamics", "Нормальная динамика");
        add ("Dense, check the drums", "Плотно, проверьте барабаны");
        add ("Over-compressed", "Пережато");
        add ("3\xe2\x80\x93" "8 kHz in range", "3–8 kHz в норме");
        add ("3\xe2\x80\x93" "8 kHz fizz above target", "«Песок» 3–8 kHz выше цели");
        // tab bar and footer
        add ("Analyse", "Анализ");
        add ("Reset on play", "Сброс при старте");
        add ("samples", "сэмплов");
        add ("Latency", "Задержка");
        return m;
    }();
    return t;
}
} // namespace mm::ui
