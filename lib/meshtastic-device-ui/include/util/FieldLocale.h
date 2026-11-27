#pragma once

#include <cstring>

extern "C" const char *lv_i18n_get_current_locale(void);

inline const char *fieldText(const char *english, const char *italian, const char *spanish)
{
    const char *locale = lv_i18n_get_current_locale();
    if (locale && strcmp(locale, "it") == 0)
        return italian;
    if (locale && strcmp(locale, "es") == 0)
        return spanish;
    return english;
}
