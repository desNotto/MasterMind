# MasterMind — установка и проверка

MasterMind — плагин для мастер-шины (VST3, Windows 64 бит).

## Установка

1. Закройте REAPER.
2. Скопируйте файл `MasterMind.vst3` в папку `C:\Program Files\Common Files\VST3\`
   (Windows попросит права администратора — нажмите «Продолжить»).
   Если там уже есть старый `MasterMind.vst3`, замените его.
3. Запустите REAPER.
4. Options → Preferences → Plug-ins → VST → кнопка **Re-scan** (или «Clear cache/re-scan»).
5. Добавьте плагин на дорожку или мастер: FX → найдите **MasterMind** (производитель desNotto).

## Удаление

Удалите `C:\Program Files\Common Files\VST3\MasterMind.vst3`.
Настройки плагина (размер окна и т. п.) лежат в `%APPDATA%\MasterMind\` — эту папку можно удалить.

## Версия

Номер версии и код сборки (commit) показаны внизу окна плагина. Сообщая о проблеме, пишите эти два значения.
