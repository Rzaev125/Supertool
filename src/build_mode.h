#pragma once
#ifndef SHIFTWHEEL_LOCAL_DEVELOPMENT
#define SHIFTWHEEL_LOCAL_DEVELOPMENT 1
#endif
namespace shiftwheel {
inline constexpr bool local_development_build = SHIFTWHEEL_LOCAL_DEVELOPMENT != 0;
inline constexpr wchar_t local_update_notice[] = L"Локальная сборка для разработки. Проверка и загрузка обновлений отключены; настройки канала сохранены.";
}
