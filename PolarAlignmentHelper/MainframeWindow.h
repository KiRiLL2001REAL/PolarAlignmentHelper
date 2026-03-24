#pragma once

#include <imgui.h>
#include <string>
#include <vector>
#include "ToupTekCameraManager.h"
#include "FrameHeader.h"
#include <gl/GL.h>

#include <chrono>
#include "SolveManager.h"
#include <filesystem>


class MainframeWindow
{
public:
    MainframeWindow(HWND hWnd);
    virtual ~MainframeWindow();

    void draw(const std::string& title);
    void setSolverAddress(const std::string& ip, int port);
    void setSaveDirectory(const std::filesystem::path& saveDirectory);
    void setSolvingTimeLimitSec(size_t timeLimitSec);

    LRESULT WINAPI WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


protected:
    static const ImVec4 COLOR_CHILD_WINDOW;

    HWND hWnd;

    ToupTekCameraManager cameraManager;
    
    std::vector<ToupTekCameraManager::DeviceIdDTO> devicesDTO;
    std::vector<ToupcamResolution> deviceResolutionsDTO;
    std::vector<ToupTekCameraManager::PixelFormatDTO> pixelFormatsDTO;
    const unsigned MAX_REASONABLE_EXPOSURE_TIME = 10'000'000;  // микросекунды
    ToupTekCameraManager::RangeDTO exposureTimeRangeDTO;
    ToupTekCameraManager::RangeDTO exposureGainRangeDTO;

    GLuint displayGlTexture;
    volatile bool              imageProcessing;
    mutable std::shared_mutex  imageMutex;
    Imaging::FrameHeader       imageHeader;
    BYTE*                      imageBuffer;
    size_t                     rgbWritePage;
    mutable std::shared_mutex  rgbMutex[2];
    Imaging::FrameHeader       rgbHeader[2];
    BYTE*                      rgbBuffer[2];

    SolveManager  solveManager;

    const size_t MOUNT_POINTS_MIN_DISPLAYED_ITEMS = 4;
    std::vector<SolveManager::CelestialPlateDTO> mountPlatesDTO;


    void drawLeftChild();
    void drawRightChild();

    void invalidateRGBImage(size_t page);
};

