#include "util/HomeLayout.h"
#include <Preferences.h>

extern "C" int tdeck_home_load(void *out, size_t size)
{
    Preferences prefs;
    if (!out || size != sizeof(meshhome::Layout) || !prefs.begin("meshhome", true))
        return 0;
    int count = 0;
    if (prefs.getBytesLength("layout") == size)
        count = prefs.getBytes("layout", out, size);
    prefs.end();
    return count;
}

extern "C" bool tdeck_home_save(const void *data, size_t size)
{
    Preferences prefs;
    if (!data || size != sizeof(meshhome::Layout) || !prefs.begin("meshhome", false))
        return false;
    const bool ok = prefs.putBytes("layout", data, size) == size;
    prefs.end();
    return ok;
}
