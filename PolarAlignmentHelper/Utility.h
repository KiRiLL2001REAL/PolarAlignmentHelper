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

    /// <summary>
    ///     Сохраняет изображение на диск в формате JPEG.
    /// </summary>
    /// <param name="data"></param>
    /// <param name="width"></param>
    /// <param name="height"></param>
    /// <param name="quality"></param>
    /// <param name="filename"></param>
    /// <returns>
    ///     True - если операция записи завершилась успешно.
    ///     False - в ином случае.
    /// </returns>
    static bool writeRGBJpeg(unsigned char* data, unsigned width, unsigned height, unsigned quality, const std::string& filename);
};
