#pragma once

#include <string>

/// <summary>
///	    Утилитарный класс для всякого.
/// </summary>
class Utility
{
public:

    /// <summary>
    ///     Функция перевода wstring строки в UTF-8.
    /// </summary>
    /// <param name="wstr">Широкая строка.</param>
    /// <returns>
    ///     Строка в кодировке UTF-8.
    /// </returns>
    static const std::string& wstringToUtf8(const std::wstring& wstr);
};

