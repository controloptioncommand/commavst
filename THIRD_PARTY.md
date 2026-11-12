# Чужой код и данные

## Шрифты Medeo
`Resources/Fonts/` — Medeo Sans, Mono, Pixel. SIL Open Font License, `Resources/Fonts/OFL.txt`.

## JUCE
[JUCE 8](https://juce.com) — фреймворк плагина, скачивается при сборке.

## clap-juce-extensions
[free-audio/clap-juce-extensions](https://github.com/free-audio/clap-juce-extensions) — формат CLAP для JUCE, лицензия MIT. Скачивается при сборке (вместе с заголовками CLAP, MIT).

## tuning-library
[surge-synthesizer/tuning-library](https://github.com/surge-synthesizer/tuning-library) — разбор Scala `.scl` и `.kbm`, расчёт частот нот. Лицензия MIT (© Paul Walker). Скачивается при сборке, используются только заголовки.

## MTS-ESP
[ODDSound MTS-ESP](https://github.com/ODDSound/MTS-ESP) — клиент и мастер MTS-ESP. Лицензия 0BSD (© ODDSound Ltd.). Скачивается при сборке. Для работы нужна библиотека `libMTS`, которую ставит любой мастер MTS-ESP; сама она в Comma не входит.

## Алгоритмы
- Определение высоты — YIN (de Cheveigné, Kawahara, 2002), реализация своя.
- Фильтр Гильберта — две цепочки allpass-фильтров с коэффициентами Олли Ниемитало (public domain).
- Перенос пиков спектра с привязкой фаз — по Laroche, Dolson (1999), реализация своя.
- TD-PSOLA — классический алгоритм, реализация своя.

## Вдохновение
Comma — rack для микротональности и MPE-глайдов. Идеи: MicroShift (Soundtoys) для дублера, книга Уильяма Сетареса «Tuning, Timbre, Spectrum, Scale» для спектрального модуля. Код, названия и интерфейс свои.
