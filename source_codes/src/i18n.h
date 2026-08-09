#pragma once
#include <windows.h>
#include <string>

enum class Language {
    AUTO = 0,
    TURKISH = 1,
    ENGLISH = 2
};

class I18n {
public:
    static const wchar_t* Get(const char* key);
};
