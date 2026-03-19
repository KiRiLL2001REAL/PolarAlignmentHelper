#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include "ToupTekCameraManager.h"
#include "ThreadWorker.h"
#include <gl/GL.h>


class MainframeWindow
{
public:
    MainframeWindow(HWND hWnd);
    virtual ~MainframeWindow();

    void draw(const std::string& title);

    LRESULT WINAPI WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


protected:
    static const ImVec4 COLOR_CHILD_WINDOW;

    HWND hWnd;

    ThreadWorker         worker;
    ToupTekCameraManager manager;
    
    std::vector<ToupTekCameraManager::DeviceIdDTO> devicesDTO;
    std::vector<ToupcamResolution> deviceResolutionsDTO;
    std::vector<ToupTekCameraManager::PixelFormatDTO> pixelFormatsDTO;
    const unsigned MAX_REASONABLE_EXPOSURE_TIME = 10'000'000;  // микросекунды
    ToupTekCameraManager::RangeDTO exposureTimeRangeDTO;
    ToupTekCameraManager::RangeDTO exposureGainRangeDTO;

    GLuint drawingTexture;
    ToupTekCameraManager::FrameHeader header;
    BYTE* grabbedImageBuffer;

    void drawLeftChild();
    void drawRightChild();
};

