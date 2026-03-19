#include "MainframeWindow.h"

#include "Utility.h"
#include "ImGuiCustom.h"

const ImVec4 MainframeWindow::COLOR_CHILD_WINDOW(0.06f, 0.06f, 0.06f, 0.94f);


static constexpr char LC_LEFT_SECTION_DEVICE_SELECT[]             = u8"Список камер";
static constexpr char LC_LEFT_COMBO_DEVICES_PREVIEW_DEFAULT[]     = u8"Нет подключенных устройств.";
static constexpr char LC_LEFT_BTN_UPDATE_DEVICES[]                = u8"Обновить";
static constexpr char LC_LEFT_BTN_DEVICE_INFO[]                   = u8"Инфо";
static constexpr char LC_LEFT_BTN_TOGGLE_DEV_CONNECT[]            = u8"Подключить";
static constexpr char LC_LEFT_BTN_TOGGLE_DEV_DISCONNECT[]         = u8"Отключить";
static constexpr char LC_LEFT_SECTION_CAPTURE_SETTINGS[]          = u8"Съемка и Разрешение";
static constexpr char LC_LEFT_COMBO_RESOLUTION_PREVIEW_DEFAULT[]  = u8"Отключено";
static constexpr char LC_LEFT_COMBO_RESOLUTION_LABEL[]            = u8"Разрешение";
static constexpr char LC_LEFT_COMBO_PIXFMT_PREVIEW_DEFAULT[]      = u8"Отключено";
static constexpr char LC_LEFT_COMBO_PIXFMT_LABEL[]                = u8"Формат пикселя";
static constexpr char LC_LEFT_SLIDER_EXPTIME_LABEL[]              = u8"Время выдержки";
static constexpr char LC_LEFT_SLIDER_EXPGAIN_LABEL[]              = u8"Усиление";


MainframeWindow::MainframeWindow(HWND hWnd):
    hWnd(hWnd),

    worker(),
    manager(&worker),
    
    devicesDTO(),
    deviceResolutionsDTO(),
    pixelFormatsDTO(),
    exposureTimeRangeDTO(),
    exposureGainRangeDTO(),

    imageProcessing(false),
    displayGlTexture(0),
    imageBuffer(NULL),
    rgbWritePage(0)
{
    memset(&imageHeader, 0, sizeof(imageHeader));
    memset(&rgbHeader[0], 0, sizeof(rgbHeader[0]));
    memset(&rgbHeader[1], 0, sizeof(rgbHeader[1]));

    glGenTextures(1, &displayGlTexture);
    glBindTexture(GL_TEXTURE_2D, displayGlTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    worker.start();
}

MainframeWindow::~MainframeWindow()
{
    worker.stop();

    devicesDTO.clear();
    deviceResolutionsDTO.clear();
    pixelFormatsDTO.clear();
    memset(&exposureTimeRangeDTO, 0, sizeof(exposureTimeRangeDTO));
    memset(&exposureGainRangeDTO, 0, sizeof(exposureGainRangeDTO));

    if (displayGlTexture) {
        glDeleteTextures(1, &displayGlTexture);
        displayGlTexture = 0;
    }

    memset(&imageHeader, 0, sizeof(imageHeader));
    if (imageBuffer) {
        free(imageBuffer);
        imageBuffer = NULL;
    }
    memset(&rgbHeader[0], 0, sizeof(rgbHeader[0]));
    if (rgbBuffer[0]) {
        free(rgbBuffer[0]);
        rgbBuffer[0] = NULL;
    }
    memset(&rgbHeader[1], 0, sizeof(rgbHeader[1]));
    if (rgbBuffer[1]) {
        free(rgbBuffer[1]);
        rgbBuffer[1] = NULL;
    }
}

void MainframeWindow::draw(
    const std::string& title
) {
    static bool first_entry = true;
    if (first_entry) {
        first_entry = false;
        manager.updateDeviceList();
    }

    static ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration;// | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(viewport->WorkSize, ImGuiCond_Always);

    if (ImGui::Begin("##mainframe", NULL, flags))
    {
        drawLeftChild();
        ImGui::SameLine();
        drawRightChild();

        if (manager.isDeviceOpened() && !manager.isDeviceRunning())
            manager.startDevicePulling(hWnd);
    }
    ImGui::End();
}

LRESULT __stdcall MainframeWindow::WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
) {
    return manager.WndProcHandler(hWnd, msg, wParam, lParam);
}

void MainframeWindow::drawLeftChild()
{
    static ImGuiChildFlags flags = ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COLOR_CHILD_WINDOW);

    // TODO ограничения на основании размера шрифта
    ImGui::SetNextWindowSizeConstraints(ImVec2(100, -1), ImVec2(500, -1));
    if (ImGui::BeginChild("##settings", ImVec2(200, ImGui::GetContentRegionAvail().y), flags, ImGuiWindowFlags_HorizontalScrollbar)) {

        bool selected_device_valid = false;
        static size_t item_device_idx = 0;

        // 1 строка: Комбо выбора камеры, Кнопка обновить, Кнопка инфо
        ImGui::SeparatorText(LC_LEFT_SECTION_DEVICE_SELECT);
        {
            // комбо выбора камеры
            ImGui::BeginDisabled(manager.isDeviceOpened());
            {
                static std::vector<std::string> items_device = {};
                // static size_t item_device_idx = 0;  // перемещён в более общую область видимости

                bool devicesDTOUpdated = false;
                devicesDTO = manager.getDevicesIdDTO(&devicesDTOUpdated);
                if (devicesDTOUpdated) {
                    item_device_idx = 0;
                    items_device.clear();
                    for (size_t i = 0; i < devicesDTO.size(); i++) {
#ifdef _WIN32
                        items_device.push_back(Utility::wstringToUtf8(devicesDTO[i].displayname));
#else
                        items_device.push_back(devicesDTO[i].displayname);
#endif
                    }
                }
                if (items_device.empty())
                    items_device.emplace_back(LC_LEFT_COMBO_DEVICES_PREVIEW_DEFAULT);

                selected_device_valid = !devicesDTO.empty();

                std::string item_device_preview = items_device[item_device_idx];
                ImGuiCustom::combo(u8"##deviceSelector", items_device, &item_device_idx, item_device_preview, 0, 260.f);
            }
            
            // кнопка обновления списка
            ImGui::SameLine();
            {
                char buffer[64];
                snprintf(buffer, sizeof(buffer), u8"%s##btnUpdateDevices", LC_LEFT_BTN_UPDATE_DEVICES);
                if (ImGui::Button(buffer)) {
                    manager.updateDeviceList();
                }
            }
            ImGui::EndDisabled();  // # ImGui::BeginDisabled(manager.isDeviceOpened());

            // кнопка вызова информационного окна
            ImGui::SameLine();
            {
                ImGui::BeginDisabled(!selected_device_valid);

                char buffer[64];
                snprintf(buffer, sizeof(buffer), u8"%s##btnDeviceInfo", LC_LEFT_BTN_DEVICE_INFO);
                if (ImGui::Button(buffer)) {

                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip) && selected_device_valid)
                    ImGui::SetTooltip(u8"Не реализовано.");

                ImGui::EndDisabled();  // # ImGui::BeginDisabled(!selected_device_valid);
            }
        }

        // 2 строка: Кнопка подключения / отключения камеры
        {
            ImGui::BeginDisabled(!selected_device_valid || manager.waitingDeviceToOpen());

            char buffer[64];
            snprintf(buffer, sizeof(buffer), u8"%s##toggleOpenCloseDevice", manager.isDeviceOpened() ? LC_LEFT_BTN_TOGGLE_DEV_DISCONNECT : LC_LEFT_BTN_TOGGLE_DEV_CONNECT);
            static ImVec2 _FramePadding = ImGui::GetStyle().FramePadding;
            float width = max(ImGui::CalcTextSize(LC_LEFT_BTN_TOGGLE_DEV_DISCONNECT).x, ImGui::CalcTextSize(LC_LEFT_BTN_TOGGLE_DEV_CONNECT).x) + _FramePadding.x * 2.f;
            if (ImGui::Button(buffer, ImVec2(width, 0))) {
                if (manager.isDeviceOpened()) manager.closeDevice();
                else                          manager.openDevice(devicesDTO[item_device_idx].id);
            }

            ImGui::EndDisabled();  // # ImGui::BeginDisabled(!selected_device_valid);
        }

        ImGui::SeparatorText(LC_LEFT_SECTION_CAPTURE_SETTINGS);

        ImGui::BeginDisabled(!(selected_device_valid && manager.isDeviceOpened()));
        // 3 строка: Разрешение
        {
            static std::vector<std::string> items_resolution = {};
            static size_t item_resolution_idx = 0;
            static size_t prev_item_resolution_idx = 1024;

            bool deviceResolutionsDTOUpdated = false;
            deviceResolutionsDTO = manager.getPreviewResolutionsDTO(&deviceResolutionsDTOUpdated);
            if (deviceResolutionsDTOUpdated) {
                prev_item_resolution_idx = 1024;
                item_resolution_idx = 0;
                items_resolution.clear();
                for (size_t i = 0; i < deviceResolutionsDTO.size(); i++) {
                    const auto& res = deviceResolutionsDTO[i];
                    char buffer[64];
                    snprintf(buffer, sizeof(buffer), u8"%ux%u", res.width, res.height);
                    items_resolution.emplace_back(buffer);
                }
            }
            if (items_resolution.empty())
                items_resolution.emplace_back(LC_LEFT_COMBO_RESOLUTION_PREVIEW_DEFAULT);

            std::string item_resolution_preview = items_resolution[item_resolution_idx];
            char buffer[64];
            snprintf(buffer, sizeof(buffer), u8"%s##resolutionSelector", LC_LEFT_COMBO_RESOLUTION_LABEL);
            ImGuiCustom::combo(buffer, items_resolution, &item_resolution_idx, item_resolution_preview, 0, 260.f);

            if (prev_item_resolution_idx != item_resolution_idx && manager.isDeviceOpened()) {
                manager.setCameraPreviewResolution(item_resolution_idx);
                prev_item_resolution_idx = item_resolution_idx;
            }
        }
        // 4 строка: Формат пикселя
        {
            static std::vector<std::string> items_pixel_format = {};
            static size_t item_pixel_format_idx = 0;
            static size_t prev_item_pixel_format_idx = 1024;

            bool pixelFormatsDTOUpdated = false;
            pixelFormatsDTO = manager.getPixelFormatsDTO(&pixelFormatsDTOUpdated);
            if (pixelFormatsDTOUpdated) {
                prev_item_pixel_format_idx = 1024;
                item_pixel_format_idx = 0;
                items_pixel_format.clear();
                for (size_t i = 0; i < pixelFormatsDTO.size(); i++)
                    items_pixel_format.push_back(pixelFormatsDTO[i].name);
            }
            if (items_pixel_format.empty())
                items_pixel_format.emplace_back(LC_LEFT_COMBO_PIXFMT_PREVIEW_DEFAULT);

            std::string item_pixel_format_preview = items_pixel_format[item_pixel_format_idx];
            char buffer[64];
            snprintf(buffer, sizeof(buffer), u8"%s##pixelFormatSelector", LC_LEFT_COMBO_PIXFMT_LABEL);
            ImGuiCustom::combo(buffer, items_pixel_format, &item_pixel_format_idx, item_pixel_format_preview, 0, 260.f);

            if (prev_item_pixel_format_idx != item_pixel_format_idx && manager.isDeviceOpened()) {
                manager.setPixelFormat(pixelFormatsDTO[item_pixel_format_idx].pixelFormat);
                prev_item_pixel_format_idx = item_pixel_format_idx;
            }
        }
        ImGui::Dummy(ImVec2(0, 8));
        // 5 строка: выдержка
        {
            static ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat;

            static unsigned current_value = 0;
            static bool firstEntry = true;

            bool exposureTimeRangeUpdated = false;
            exposureTimeRangeDTO = manager.getExposureTimeRange(&exposureTimeRangeUpdated);
            if (exposureTimeRangeUpdated)
                current_value = exposureTimeRangeDTO.def;

            float _min = float(exposureTimeRangeDTO.min) * 0.001f;
            float _max = float(min(exposureTimeRangeDTO.max, MAX_REASONABLE_EXPOSURE_TIME)) * 0.001f;
            float val = float(current_value) * 0.001f;

            char buffer[64];
            snprintf(buffer, sizeof(buffer), u8"%s##exposureTime", LC_LEFT_SLIDER_EXPTIME_LABEL);
            ImGui::SetNextItemWidth(260.f);
            ImGui::SliderFloat(buffer, &val, _min, _max, u8"%.3f ms", flags);

            unsigned new_value = unsigned(std::roundf(val * 1000));
            if (new_value != current_value || (firstEntry && manager.isDeviceOpened())) {
                firstEntry = false;
                if (manager.setExposureTime(new_value, &new_value)) {
                    current_value = new_value;
                }
            }
        }
        // 6 строка: усиление
        {
            static ImGuiSliderFlags flags = ImGuiSliderFlags_AlwaysClamp | ImGuiSliderFlags_Logarithmic | ImGuiSliderFlags_NoRoundToFormat;
            
            static unsigned current_value = 0;
            static bool firstEntry = true;
        
            bool exposureGainRangeUpdated = false;
            exposureGainRangeDTO = manager.getExposureGainRange(&exposureGainRangeUpdated);
            if (exposureGainRangeUpdated)
                current_value = exposureGainRangeDTO.def;
        
            int _min = exposureGainRangeDTO.min;
            int _max = exposureGainRangeDTO.max;
            int val = current_value;
        
            char buffer[64];
            snprintf(buffer, sizeof(buffer), u8"%s##exposureGain", LC_LEFT_SLIDER_EXPGAIN_LABEL);
            ImGui::SetNextItemWidth(260.f);
            ImGui::SliderInt(buffer, &val, _min, _max, u8"%d %%", flags);
        
            if (val != current_value || (firstEntry && manager.isDeviceOpened())) {
                firstEntry = false;
                if (manager.setExposureGain(val))
                    current_value = val;
            }
        }
        ImGui::EndDisabled();  // # ImGui::BeginDisabled(!(selected_device_valid && manager.isDeviceOpened()));
        ImGui::Dummy(ImVec2(0, 32));

        ImGui::EndChild();
    }

    ImGui::PopStyleColor();
}

void MainframeWindow::drawRightChild()
{
    static ImGuiChildFlags flags = ImGuiChildFlags_Borders;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, COLOR_CHILD_WINDOW);

    if (ImGui::BeginChild("##viewport", ImVec2(0, ImGui::GetContentRegionAvail().y), flags)) {

        // Хаваем картинку и выполняем debayer
        if (manager.grabImageData(imageHeader, imageBuffer)) {
            if (!imageHeader.isRaw) {
                memcpy(&rgbHeader[rgbWritePage], &imageHeader, sizeof(imageHeader));
                memcpy(&rgbBuffer[rgbWritePage], &imageBuffer, imageHeader.buffer_size);
                rgbWritePage = 1 - rgbWritePage;
            }
            else {
                if (!imageProcessing) {
                    imageProcessing = true;
                    std::thread thr([&]()
                        {
                            std::unique_lock lock(rgbMutex[rgbWritePage]);
                            auto start = std::chrono::high_resolution_clock::now();
                            manager.debayerRawImage(
                                imageHeader,
                                imageBuffer,
                                rgbHeader[rgbWritePage],
                                rgbBuffer[rgbWritePage]
                            );
                            auto elapsed = std::chrono::high_resolution_clock::now() - start;
                            printf("Debayer in %lld ms\n", std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
                            rgbWritePage = 1 - rgbWritePage;
                            imageProcessing = false;
                        }
                    );
                    thr.detach();
                }
            }
        }

        {
            size_t page = 1 - rgbWritePage;
            std::shared_lock lock(rgbMutex[page]);
            ToupTekCameraManager::FrameHeader h = rgbHeader[page];
            if (h.width && h.height) {
                glBindTexture(GL_TEXTURE_2D, displayGlTexture);
                unsigned short internalFmt = rgbHeader[page].bpp <= 24 ? GL_RGB8 : GL_RGB16;
                unsigned short typ         = rgbHeader[page].bpp <= 24 ? GL_UNSIGNED_BYTE : GL_UNSIGNED_SHORT;
                glPixelStorei(GL_UNPACK_ALIGNMENT, 3);
                glTexImage2D(
                    GL_TEXTURE_2D,
                    0,
                    internalFmt,
                    rgbHeader[page].width,
                    rgbHeader[page].height,
                    0,
                    GL_RGB,
                    typ,
                    rgbBuffer[page]);
            }
        }
        

        if (displayGlTexture && imageHeader.width && imageHeader.height) {
            ImVec2 space = ImGui::GetContentRegionAvail();

            float arImage  = float(imageHeader.width) / imageHeader.height;
            float arWindow = space.x / space.y;

            ImVec2 imgSize;

            if (arWindow < arImage) {
                imgSize.x = space.x;
                imgSize.y = space.x / arImage;
            }
            else {
                imgSize.y = space.y;
                imgSize.x = space.y * arImage;
            }
            
            static ImVec2 padding = ImGui::GetStyle().FramePadding;
            ImGui::SetCursorPos(ImVec2((space.x - imgSize.x)/2 + padding.x * 2, (space.y - imgSize.y) / 2 + padding.y * 2.5f));
            ImGui::Image((ImTextureID)(intptr_t)displayGlTexture, imgSize);
        }

        ImGui::EndChild();
    }

    ImGui::PopStyleColor();
}
