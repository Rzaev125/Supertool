# Подключение настоящего ключа обновлений

Для Rzaev125/Supertool создан постоянный ECDSA P-256 ключ `shiftwheel-prod-1`.
Публичный ключ: BCRYPT_ECCPUBLIC_BLOB72 байта (ECS1). SHA256 публичного blob:
`f6696881802a9b0ab6d2a025e29abdcb87fbc0ac11fb5c86e5f9457d0cd0a391`.

Локальные файлы: `%LOCALAPPDATA%\ShiftWheelSigning` обычного профиля Windows:

- `update-private.pem` — секретный PKCS8 PEM; не добавлять в Git/пакеты/чат.
- `update-public.hex` — публичный pin144hex.
- `key-id.txt` — идентификатор.

Папка создана вне репозитория и sandbox AppData. Наследование ACL отключено;
доступ разрешён только SID владельца и SYSTEM, права приватного файла проверены.
Нужно хранить отдельную защищённую резервную копию: GitHub не позволяет скачать
назад значение секрета. Повторное создание/автоматическая ротация не выполнялись.

## GitHub

В environment `release` созданы и проверены:

| Тип | Имя | Назначение |
| --- | --- | --- |
| Variable | UPDATE_PUBLIC_KEY_HEX | Совпадает с локальным публичным pin |
| Variable | UPDATE_KEY_ID | shiftwheel-prod-1 |
| Secret | UPDATE_SIGNING_KEY_PEM | Приватный PEM, передан в API зашифрованным sealed box |

Доступ к environment требует ручного подтверждения владельца Rzaev125. Значение
секрета не читалось обратно: проверены успешная запись и metadata имени/времени,
а соответствие пары подтверждено локально до передачи.

На GitHub main добавлен только ручной workflow
`.github/workflows/verify-update-key.yml`, commit
`a8b708a9b5dfc44f93b1939fdbccfe99d644d1e0`. Он проверяет соответствие pin,
P-256/P1363 подпись и отказ изменённым данным, выводит только публичные ID/fingerprint.
В нём нет checkout, сторонних actions, загрузки артефактов или выпуска приложения.
Проверка зарегистрирована GitHub как active:
[Verify update signing key](https://github.com/Rzaev125/Supertool/actions/workflows/verify-update-key.yml).
Сам workflow не запускался; его PowerShell-тело выполнено локально с реальным ключом — PASS.

Для удалённой проверки: Actions → Verify update signing key → Run workflow,
затем подтвердить environment release. Это проверка ключа, не публикация.

## Локальное подтверждение и границы

Настоящая подпись создана существующим `scripts/sign-update-manifest.ps1`;
`signature_tests.exe` принял её с новым pin и отклонил подменённый и неподписанный
манифесты. Проверочные файлы находятся только в build/production-update-key-check;
каналы updates/stable*.txt и готовые пакеты0.15 не менялись.

На удалённом main пока нет полного `.github/workflows/release.yml`. Его локальная
версия уже использует эти3 значения, но публикация актуальных исходников/release
workflow и подключение Windows Code Signing остаются отдельными шагами.
Обычные пакеты0.15 по-прежнему не содержат production pin и не имеют Authenticode.
Подключение ключа к GitHub само по себе не меняет доверие уже собранных EXE.

Локальные несекретные отчёты: build/production-update-key-github.json и
build/production-update-key-workflow-github.json. Приватного PEM в этих файлах нет.
