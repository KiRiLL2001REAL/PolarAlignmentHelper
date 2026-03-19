#include "Utility.h"

#include <utf8.h>

const std::string& Utility::wstringToUtf8(const std::wstring& wstr)
{
    thread_local std::string result;
    result.clear();
    utf8::utf16to8(
        wstr.data(),
        wstr.data() + wstr.size(),
        std::back_inserter(result)
    );
    return result;
}
