#pragma once

#include <imgui.h>

#include <string>
#include <vector>

/// <summary>
///		Утилитарный класс для отрисовки некоторых виджетов ImGui.
/// </summary>
class ImGuiCustom
{
public:
    /// <summary>
    ///     Функция отрисовки ImGui Combo.
    /// </summary>
    /// <param name="label">Название виджета.</param>
    /// <param name="items">Список элементов.</param>
    /// <param name="selectedItemIdx">Индекс выбранного элемента.</param>
    /// <param name="preview">Отображаемое значение.</param>
    /// <param name="flags">ImGui флаги для Combo.</param>
    /// <param name="width">Ширина виджета. <=0 для автоматического выбора ширины.</param>
    /// <param name="disabled">Флаг отключения виджета.</param>
    static void combo(
        const std::string& label,
        const std::vector<std::string>& items,
        size_t* selectedItemIdx,
        const std::string& preview,
        ImGuiComboFlags flags = 0,
        float width = 200.f,
        bool disabled = false);


};

