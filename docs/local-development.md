# Локальная разработка ShiftWheel

Разработка ведётся в `D:\IT\Project\Supertool`. Локальные проверки не публикуют
релизы, не изменяют публичные каналы и не устанавливают приложение.

## Проверки одной командой

После сборки Release:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-local.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-local.ps1 -Suite Conversions
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/check-local.ps1 -Suite Full
```

Можно использовать `pwsh` вместо `powershell`. Python берётся из CMakeCache;
при необходимости задайте `-Python C:\path\to\python.exe`. Нужны тестовые пакеты
из `tests/requirements.txt`, CTest, собранные тестовые EXE и соответствующие модули.
Сценарий не загружает эти зависимости автоматически.

- `Smoke` — короткие реальные PNG → JPG, MOV → MP4, WAV → MP3 и DOCX → PDF.
  Проверяются размеры/пиксели, потоки и полное декодирование медиа, текст PDF и
  неизменность оригиналов.
- `Conversions` — десять групп проверок, включая AVIF/HEIC, Office, презентации,
  PDF и OCR на русском, английском и азербайджанском.
- `Full` — все зарегистрированные CTest-проверки. Если запущен основной экземпляр
  ShiftWheel, `application_smoke` явно пропускается: работающий экземпляр не
  закрывается ради проверки. Пропуски отражаются в отчёте. Ручная проверка жеста
  в Проводнике остаётся отдельной приёмкой.

Результат последнего запуска: `build/local-check/report.json`, `check.log` и
`results.xml`. Успешные фикстуры удаляются. При сбое рабочая папка сохраняется под
`build/local-check/runs/<id>` для диагностики; команда возвращает ошибку. Отсутствие
нужного теста или пропуск всех тестов не считаются успехом.

## Очистка оставшихся фикстур

```powershell
# Сначала посмотреть список:
powershell -NoProfile -File scripts/check-local.ps1 -Cleanup
# Удалить перечисленные принадлежащие runner папки:
powershell -NoProfile -File scripts/check-local.ps1 -Cleanup -Apply
```

Очистка работает только для помеченных папок этого runner. Активные проверки
защищены блокировкой. Чужие папки, результаты конвертации, `tools/modules`,
`out/module-assets` и кэш загрузок не удаляются. Junction/symlink при удалении
отклоняются; содержимое целевой папки ссылки не затрагивается.
