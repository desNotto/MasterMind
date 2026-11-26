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
        // Master workspace (mockups v5-v9)
        add ("Gain reduction, last 10 s", "Ослабление (GR), последние 10 с");
        add ("All parameters of the module are here. Hover any control for advice.", "Здесь все параметры модуля. Наведите курсор на элемент, чтобы увидеть совет.");
        add ("trim, polarity and the subsonic filter", "уровень, полярность и фильтр Subsonic");
        add ("10 bands, dynamic per band", "10 полос, каждая может быть динамической");
        add ("5-band dynamics, drag the yellow handles to move the crossovers", "компрессия в 5 полосах, жёлтые ручки двигают частоты раздела");
        add ("mono maker, phase rotator, translator", "моно низа, поворот фазы, транслятор");
        add ("glue compressor for the whole mix", "компрессор «склейки» для всего микса");
        add ("three bands, oversampled, drag a band up or down to set its drive", "три полосы, тяните полосу вверх или вниз, чтобы изменить Drive");
        add ("width per band and the correlation guard", "ширина по полосам и защита корреляции");
        add ("shaves drum peaks before the limiter", "срезает пики барабанов перед лимитером");
        add ("true-peak-safe look-ahead limiter", "лимитер с упреждением и контролем true peak");
        add ("final gain and dither", "итоговый уровень и дизеринг");
        add ("select it on the spectrum or with the numbers", "выберите полосу на спектре или номером");
        add ("cuts narrow ringing peaks", "убирает узкие звенящие пики");
        add ("Click a number to select", "Клик по номеру выбирает полосу");
        add ("edit points on the spectrum", "точки меняются на спектре");
        add ("Oversampling follows Quality", "Передискретизация по настройке Quality");
        add ("Double-click: add a band", "Двойной клик — новая полоса");
        add ("at the edges: low cut / high cut", "у краёв — Low cut / High cut");
        add ("open Eyes for history", "история — на вкладке Eyes");
        add ("hover for advice", "наведите курсор для совета");
        add ("10 s", "10 с");
        add ("Ref set", "Референсы");
        add ("Default", "По умолчанию");
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
        add ("Target: Streaming ref", "Цель: стриминг");
        add ("LUFS reference (info only)", "LUFS — ориентир (справка)");
        add ("mono below", "моно ниже");
        add ("Balance L/R", "Баланс L/R");
        add ("Below ceiling", "Ниже Ceiling");
        add ("Above ceiling", "Выше Ceiling");
        add ("Louder than", "Громче");
        add ("Quieter than", "Тише");
        add ("(info only)", "(справка)");
        add ("Corr", "Корр.");
        add ("Mono loss##table", "Потеря");
        add ("Reset on play", "Сброс при старте");
        add ("samples", "сэмплов");
        add ("Latency", "Задержка");
        return m;
    }();
    return t;
}
} // namespace mm::ui
