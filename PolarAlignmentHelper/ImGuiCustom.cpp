#include "ImGuiCustom.h"

void ImGuiCustom::combo(
    const std::string& label,
    const std::vector<std::string>& items,
    size_t* selectedItemIdx,
    const std::string& preview,
    ImGuiComboFlags flags,
    float width,
    bool disabled
) {
    ImGui::BeginDisabled(disabled);
    if (width > 0)
        ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(label.c_str(), preview.c_str(), flags)) {
        for (size_t i = 0; i < items.size(); i++) {
            const bool is_selected = (i == *selectedItemIdx);
            if (ImGui::Selectable(items[i].c_str(), is_selected))
                *selectedItemIdx = i;
            if (is_selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}
