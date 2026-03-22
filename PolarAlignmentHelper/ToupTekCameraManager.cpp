#include "ToupTekCameraManager.h"

#include <cstring>
#include <cstdio>

#include "Debayer.h"

ToupTekCameraManager::ToupTekCameraManager(ThreadWorker* worker) :
    m_devicesCount(0),
    m_devicesMutex(),

    m_pendingHandles(0),
    m_openedDeviceIdx(TOUPCAM_MAX),
    m_handleMutex(),
    m_handle(NULL),
    m_isRunning(false),

    m_worker(worker),
    
    m_needRenewDevicesIdDTO(true),          m_devicesIdDTO(),
    m_needRenewDeviceResolutionsDTO(false), m_deviceResolutionsDTO(),
    m_needRenewPixelFormatsDTO(false),      m_pixelFormatsDTO(),
    m_needRenewExposureTimeRange(false),    m_exposureTimeRange({ 0, 0 }),
    m_needRenewGainRange(false),            m_gainRange({ 0, 0 }),

    m_pullingBitmapWritePage(0)
{
    {
        std::unique_lock lock(m_devicesMutex);
        memset(m_devices, 0, sizeof(ToupcamDeviceV2) * TOUPCAM_MAX);
    }
    invalidatePullingBitmap(0);
    invalidatePullingBitmap(1);
}

ToupTekCameraManager::~ToupTekCameraManager()
{
    closeDevice();
    
    m_needRenewDevicesIdDTO = false;         m_devicesIdDTO.clear();
    m_needRenewDeviceResolutionsDTO = false; m_deviceResolutionsDTO.clear();
    m_needRenewPixelFormatsDTO = false;      m_pixelFormatsDTO.clear();
    m_needRenewExposureTimeRange = false;    m_exposureTimeRange = { 0, 0 };
    m_needRenewGainRange = false;            m_gainRange = { 0, 0 };
    
    invalidatePullingBitmap(0);
    invalidatePullingBitmap(1);
}

void ToupTekCameraManager::updateDeviceList()
{
    static bool first_entry = true;
    if (first_entry) {
        first_entry = false;
        m_worker->setCallbackUpdateDeviceList([this](int count, ToupcamDeviceV2* devices)
            {
                onUpdateDeviceList(count, devices);
            }
        );
    }
    
    auto task_id = ThreadWorker::TASK_ID::updateDeviceList;
    auto task_meta = ThreadWorker::TASK_META();
    printf("[D] ToupTekCameraManager::updateDeviceList: register event.\n");
    m_worker->addTask(task_id, task_meta);
}

const std::vector<ToupTekCameraManager::DeviceIdDTO>& ToupTekCameraManager::getDevicesIdDTO(
    bool* p_dtoUpdated
) {
    bool updated = false;
    if (m_needRenewDevicesIdDTO) {
        m_needRenewDevicesIdDTO = false;
        updated = true;
        m_devicesIdDTO.clear();
        {
            std::shared_lock lock(m_devicesMutex);
            for (size_t i = 0; i < m_devicesCount; i++) {
                static DeviceIdDTO dto;
                dto.displayname = m_devices[i].displayname;
                dto.id = m_devices[i].id;
                m_devicesIdDTO.push_back(dto);
            }
        }
    }
    if (p_dtoUpdated)
        *p_dtoUpdated = updated;
    return m_devicesIdDTO;
}

const ToupcamDeviceV2 ToupTekCameraManager::getDeviceInfo(
    int idx,
    bool* p_success
) const
{
    ToupcamDeviceV2 result;
    memset(&result, 0, sizeof(ToupcamDeviceV2));

    bool success = false;
    {
        std::shared_lock lock(m_devicesMutex);
        if (idx < m_devicesCount) {
            success = true;
            result = m_devices[idx];
        }
    }

    if (p_success)
        *p_success = success;

    return result;
}

void ToupTekCameraManager::wprintDeviceInfo(
    int idx
) const
{
    if (idx >= m_devicesCount)
        return;
    auto info = getDeviceInfo(idx);
    wprintf(L"DisplayName : %ls\n", info.displayname);
    wprintf(L"Id          : %ls\n", info.id);
    auto model = info.model;
    wprintf(L"Model\n");
    wprintf(L"  Name             : %ls\n" , model->name);
    wprintf(L"  Flag             : %llu\n", model->flag);
    {
        // --- Тип сенсора и архитектура ---
        if (model->flag & TOUPCAM_FLAG_CMOS)                wprintf(L"    - CMOS sensor.\n");
        if (model->flag & TOUPCAM_FLAG_CCD_PROGRESSIVE)     wprintf(L"    - Progressive CCD sensor.\n");
        if (model->flag & TOUPCAM_FLAG_CCD_INTERLACED)      wprintf(L"    - Interlaced CCD sensor.\n");
        if (model->flag & TOUPCAM_FLAG_GHOPTO)              wprintf(L"    - GHOPTO sensor.\n");
        if (model->flag & TOUPCAM_FLAG_LINESCAN)            wprintf(L"    - Line scan camera.\n");
        if (model->flag & TOUPCAM_FLAG_GLOBALSHUTTER)       wprintf(L"    - Global shutter.\n");
        if (model->flag & TOUPCAM_FLAG_MONO)                wprintf(L"    - Monochromatic\n");
        // --- Форматы пикселей (Pixel Formats) ---
        if (model->flag & TOUPCAM_FLAG_RAW8)                wprintf(L"    - Pixel format, RAW 8 bits.\n");
        if (model->flag & TOUPCAM_FLAG_RAW10)               wprintf(L"    - Pixel format, RAW 10bits.\n");
        if (model->flag & TOUPCAM_FLAG_RAW11)               wprintf(L"    - Pixel format, RAW 11bits.\n");
        if (model->flag & TOUPCAM_FLAG_RAW12)               wprintf(L"    - Pixel format, RAW 12bits.\n");
        if (model->flag & TOUPCAM_FLAG_RAW14)               wprintf(L"    - Pixel format, RAW 14bits.\n");
        if (model->flag & TOUPCAM_FLAG_RAW16)               wprintf(L"    - Pixel format, RAW 16bits.\n");
        if (model->flag & TOUPCAM_FLAG_RAW10PACK)           wprintf(L"    - Pixel format, RAW 10bits packed.\n");
        if (model->flag & TOUPCAM_FLAG_RAW12PACK)           wprintf(L"    - Pixel format, RAW 12bits packed.\n");
        if (model->flag & TOUPCAM_FLAG_RAW14PACK)           wprintf(L"    - Pixel format, RAW 14bits packed.\n");
        if (model->flag & TOUPCAM_FLAG_YUV411)              wprintf(L"    - Pixel format, yuv411.\n");
        if (model->flag & TOUPCAM_FLAG_VUYY)                wprintf(L"    - Pixel format, yuv422, VUYY.\n");
        if (model->flag & TOUPCAM_FLAG_UYVY)                wprintf(L"    - Pixel format, yuv422, UYVY.\n");
        if (model->flag & TOUPCAM_FLAG_YUV444)              wprintf(L"    - Pixel format, yuv444.\n");
        if (model->flag & TOUPCAM_FLAG_RGB888)              wprintf(L"    - Pixel format, RGB888.\n");
        if (model->flag & TOUPCAM_FLAG_GMCY8)               wprintf(L"    - Pixel format, GMCY, 8bits.\n");
        if (model->flag & TOUPCAM_FLAG_GMCY12)              wprintf(L"    - Pixel format, GMCY, 12bits.\n");
        // --- Интерфейс подключения (Interface) ---
        if (model->flag & TOUPCAM_FLAG_USB30)               wprintf(L"    - USB3.0\n");
        if (model->flag & TOUPCAM_FLAG_USB30_OVER_USB20)    wprintf(L"    - USB3.0 camera connected to USB2.0 port.\n");
        if (model->flag & TOUPCAM_FLAG_USB32)               wprintf(L"    - USB 3.2 Gen 2.\n");
        if (model->flag & TOUPCAM_FLAG_USB32_OVER_USB30)    wprintf(L"    - USB 3.2 Gen 2 camera connected to USB3.0 port.\n");
        if (model->flag & TOUPCAM_FLAG_GIGE)                wprintf(L"    - 1 Gigabit GigE.\n");
        if (model->flag & TOUPCAM_FLAG_25GIGE)              wprintf(L"    - 2.5 Gigabit GigE.\n");
        if (model->flag & TOUPCAM_FLAG_5GIGE)               wprintf(L"    - 5 Gigabit GigE.\n");
        if (model->flag & TOUPCAM_FLAG_10GIGE)              wprintf(L"    - 10 Gigabit GigE.\n");
        if (model->flag & TOUPCAM_FLAG_40GIGE)              wprintf(L"    - 40 Gigabit GigE.\n");
        if (model->flag & TOUPCAM_FLAG_CXP)                 wprintf(L"    - CXP: CoaXPress.\n");
        if (model->flag & TOUPCAM_FLAG_ST4)                 wprintf(L"    - ST4 port.\n");
        // --- Охлаждение и температура (Cooling & Temp) ---
        if (model->flag & TOUPCAM_FLAG_TEC)                 wprintf(L"    - Thermoelectric Cooler.\n");
        if (model->flag & TOUPCAM_FLAG_TEC_ONOFF)           wprintf(L"    - Thermoelectric Cooler can be turn on or off, support to set the target temperature of TEC.\n");
        if (model->flag & TOUPCAM_FLAG_FAN)                 wprintf(L"    - Cooling fan.\n");
        if (model->flag & TOUPCAM_FLAG_HEAT)                wprintf(L"    - Support heat to prevent fogging up.\n");
        if (model->flag & TOUPCAM_FLAG_GETTEMPERATURE)      wprintf(L"    - Support to get the temperature of the sensor.\n");
        // --- Управление изображением и производительность (Image Control & Performance) ---
        if (model->flag & TOUPCAM_FLAG_CG)                  wprintf(L"    - Conversion Gain: HCG, LCG.\n");
        if (model->flag & TOUPCAM_FLAG_CGHDR)               wprintf(L"    - Conversion Gain: HCG, LCG, HDR.\n");
        if (model->flag & TOUPCAM_FLAG_HIGH_FULLWELL)       wprintf(L"    - High fullwell capacity.\n");
        if (model->flag & TOUPCAM_FLAG_LOW_NOISE)           wprintf(L"    - Support low noise mode (Higher signal noise ratio, lower frame rate).\n");
        if (model->flag & TOUPCAM_FLAG_ROI_HARDWARE)        wprintf(L"    - Support hardware ROI.\n");
        if (model->flag & TOUPCAM_FLAG_BINSKIP_SUPPORTED)   wprintf(L"    - Support bin/skip mode, see Toupcam_put_Mode and Toupcam_get_Mode.\n");
        if (model->flag & TOUPCAM_FLAG_BLACKLEVEL)          wprintf(L"    - Support set and get the black level.\n");
        if (model->flag & TOUPCAM_FLAG_LEVELRANGE_HARDWARE) wprintf(L"    - Hardware level range, put(get)_LevelRangeV2.\n");
        if (model->flag & TOUPCAM_FLAG_ISP)                 wprintf(L"    - ISP (Image Signal Processing) chip.\n");
        if (model->flag & TOUPCAM_FLAG_PRECISE_FRAMERATE)   wprintf(L"    - Support precise framerate & bandwidth, see TOUPCAM_OPTION_PRECISE_FRAMERATE & TOUPCAM_OPTION_BANDWIDTH.\n");
        // --- Буферизация и память (Buffer & Memory) ---
        if (model->flag & TOUPCAM_FLAG_BUFFER)              wprintf(L"    - Frame buffer.\n");
        if (model->flag & TOUPCAM_FLAG_DDR)                 wprintf(L"    - Use very large capacity DDR (Double Data Rate SDRAM) for frame buffer. The capacity is not less than one full frame.\n");
        // --- Триггеры и события (Triggers & Events) ---
        if (model->flag & TOUPCAM_FLAG_TRIGGER_SOFTWARE)    wprintf(L"    - Support software trigger.\n");
        if (model->flag & TOUPCAM_FLAG_TRIGGER_EXTERNAL)    wprintf(L"    - Support external trigger.\n");
        if (model->flag & TOUPCAM_FLAG_TRIGGER_SINGLE)      wprintf(L"    - Only support trigger single: one trigger, one image.\n");
        if (model->flag & TOUPCAM_FLAG_SELFTRIGGER)         wprintf(L"    - Self trigger.\n");
        if (model->flag & TOUPCAM_FLAG_EVENT_HARDWARE)      wprintf(L"    - Hardware event, such as exposure start & stop.\n");
        // --- Фокусировка (Focus) ---
        if (model->flag & TOUPCAM_FLAG_FOCUSMOTOR)          wprintf(L"    - Support focus motor.\n");
        if (model->flag & TOUPCAM_FLAG_AUTO_FOCUS)          wprintf(L"    - Support auto focus.\n");
        if (model->flag & TOUPCAM_FLAG_AUTOFOCUSER)         wprintf(L"    - Astro auto focuser.\n");
        // --- Прочее (Other) ---
        if (model->flag & TOUPCAM_FLAG_FILTERWHEEL)         wprintf(L"    - Astro filter wheel.\n");
        if (model->flag & TOUPCAM_FLAG_CAMERALINK)          wprintf(L"    - Camera link.\n");
        if (model->flag & TOUPCAM_FLAG_LIGHTSOURCE)         wprintf(L"    - Embedded light source.\n");
        if (model->flag & TOUPCAM_FLAG_LIGHT_SOURCE)        wprintf(L"    - Stand alone light source.\n");
    }
    wprintf(L"  MaxSpeed (count) : %u\n"  , model->maxspeed);
    wprintf(L"  Preview (count)  : %u\n"  , model->preview);
    wprintf(L"  Still (count)    : %u\n"  , model->still);
    wprintf(L"  MaxFanSpeed      : %u\n"  , model->maxfanspeed);
    wprintf(L"  IOCtrol (count)  : %u\n"  , model->ioctrol);
    wprintf(L"  PixelSizeX (um)  : %.3f\n", model->xpixsz);
    wprintf(L"  PixelSizeY (um)  : %.3f\n", model->ypixsz);
    wprintf(L"  Resolutions\n");
    for (size_t i = 0; i < 16; i++)
        if (model->res[i].width)
            wprintf(L"    - %ux%u\n", model->res[i].width, model->res[i].height);
}

bool ToupTekCameraManager::isDeviceOpened() const
{
    std::shared_lock lock(m_handleMutex);
    return m_handle;
}

bool ToupTekCameraManager::openDevice(
    const std::wstring& id
) {
    static bool firstEntry = true;
    if (firstEntry) {
        firstEntry = false;
        m_worker->setCallbackDeviceOpen([this](HToupCam handle, std::wstring id)
            {
                onDeviceOpen(handle, id);
            }
        );
    }

    if (getDeviceHandle())
        return false;

    auto taskId = ThreadWorker::TASK_ID::openDevice;
    auto taskMeta = ThreadWorker::TASK_META();
    {  // taskMeta.openCamera.id = id
        size_t bytes = id.length() + 1;
#ifdef _WIN32
        bytes <<= 1;
#endif
        bytes = min(bytes, sizeof(taskMeta.openDevice.id));
        memcpy(&taskMeta.openDevice.id, id.c_str(), bytes);
        taskMeta.openDevice.id[63] = 0;
    }

    m_pendingHandles++;
    printf("[D] ToupTekCameraManager::openDevice: register event.\n");
    m_worker->addTask(taskId, taskMeta);
    return true;
}

bool ToupTekCameraManager::waitingDeviceToOpen() const
{
    return m_pendingHandles > 0;
}

HToupcam ToupTekCameraManager::getDeviceHandle()
{
    std::shared_lock lock(m_handleMutex);
    return m_handle;
}

void ToupTekCameraManager::closeDevice()
{
    while (m_worker->deviceInUse()) {
        std::this_thread::yield();
    }
    if (getDeviceHandle()) {
        std::unique_lock lock(m_handleMutex);

        printf("[D] ToupTekCameraManager::closeDevice\n");
        Toupcam_Close(m_handle);
        m_isRunning = false;
        m_handle = NULL;
        m_openedDeviceIdx = TOUPCAM_MAX;
        m_needRenewDeviceResolutionsDTO = true;
        m_needRenewPixelFormatsDTO = true;
    }
}

const std::vector<ToupcamResolution>& ToupTekCameraManager::getPreviewResolutionsDTO(
    bool* p_dtoUpdated
) {
    bool updated = false;
    if (m_needRenewDeviceResolutionsDTO) {
        m_needRenewDeviceResolutionsDTO = false;
        updated = true;
        m_deviceResolutionsDTO.clear();
        {
            std::shared_lock lock(m_handleMutex);
            if (m_handle) {
                static ToupcamModelV2 model;
                {
                    std::shared_lock lock(m_devicesMutex);
                    model = *m_devices[m_openedDeviceIdx].model;
                }
                for (size_t i = 0; i < model.preview; i++)
                    m_deviceResolutionsDTO.push_back(model.res[i]);
            }
        }
    }
    if (p_dtoUpdated)
        *p_dtoUpdated = updated;
    return m_deviceResolutionsDTO;
}

bool ToupTekCameraManager::setCameraPreviewResolution(
    size_t eSizeTarget
) {
    static bool firstEntry = true;
    if (firstEntry) {
        firstEntry = false;
        m_worker->setCallbackPreviewResolutionSet([this]()
            {
                m_isRunning = false;
            }
        );
    }

    if (!getDeviceHandle())
        return false;

    auto taskId = ThreadWorker::TASK_ID::setPreviewResolution;
    auto taskMeta = ThreadWorker::TASK_META();
    taskMeta.setPreviewResolution.p_handleMutex = &m_handleMutex;
    taskMeta.setPreviewResolution.p_handle = &m_handle;
    taskMeta.setPreviewResolution.eSizeTarget = (unsigned)eSizeTarget;

    printf("[D] ToupTekCameraManager::setCameraPreviewResolution: register event.\n");
    m_worker->addTask(taskId, taskMeta);

    return true;

    // int width, height;
    // 
    // if (FAILED(Toupcam_get_Size(handle, &width, &height)))
    //     return false;
    // 
    // printf("Width: %d, Height: %d\t\t", width, height);
}

const std::vector<ToupTekCameraManager::PixelFormatDTO>& ToupTekCameraManager::getPixelFormatsDTO(
    bool* p_dtoUpdated
) {
    bool updated = false;
    if (m_needRenewPixelFormatsDTO) {
        m_needRenewPixelFormatsDTO = false;
        updated = true;
        m_pixelFormatsDTO.clear();
        {
            std::shared_lock lock(m_handleMutex);
            if (m_handle) {
                int pixelFormatCount = 0;
                if (FAILED(Toupcam_get_PixelFormatSupport(m_handle, -1, &pixelFormatCount)))
                    printf("[E] ToupTekCameraManager::getPixelFormatsDTO: Toupcam_get_PixelFormatSupport failed.\n");
                else {
                    for (int i = 0; i < pixelFormatCount; i++)
                    {
                        static PixelFormatDTO dto;
                        Toupcam_get_PixelFormatSupport(m_handle, i, &dto.pixelFormat);
                        dto.name = Toupcam_get_PixelFormatName(dto.pixelFormat);
                        m_pixelFormatsDTO.push_back(dto);
                    }
                }
            }
        }
    }
    if (p_dtoUpdated)
        *p_dtoUpdated = updated;
    return m_pixelFormatsDTO;
}

bool ToupTekCameraManager::setPixelFormat(
    int targetPixelFormat
) {
    std::unique_lock lock(m_handleMutex);
    if (!m_handle) {
        return false;
    }

    printf("[D] ToupTekCameraManager::setPixelFormat (%d).\n", targetPixelFormat);
    HRESULT hr = Toupcam_put_Option(m_handle, TOUPCAM_OPTION_PIXEL_FORMAT, targetPixelFormat);
    if (FAILED(hr)) {
        printf("[W] ToupTekCameraManager::setPixelFormat: failed (code: %ld).\n", hr);
        return false;
    }

    return true;
}

const ToupTekCameraManager::RangeDTO& ToupTekCameraManager::getExposureTimeRange(
    bool* p_dtoUpdated
) {
    bool updated = false;
    if (m_needRenewExposureTimeRange) {
        m_needRenewExposureTimeRange = false;
        updated = true;
        unsigned _min = 0, _max = 0, _default = 0;
        {
            std::shared_lock lock(m_handleMutex);
            if (m_handle)
                Toupcam_get_ExpTimeRange(m_handle, &_min, &_max, &_default);
        }
        m_exposureTimeRange.min = _min;
        m_exposureTimeRange.max = _max;
        m_exposureTimeRange.def = _default;
    }
    if (p_dtoUpdated)
        *p_dtoUpdated = updated;
    return m_exposureTimeRange;
}

bool ToupTekCameraManager::setExposureTime(
    unsigned targetTimeMicroseconds,
    unsigned* realTimeMicroseconds
) const
{
    std::unique_lock lock(m_handleMutex);

    if (!m_handle) {
        return false;
    }

    if (FAILED(Toupcam_put_ExpoTime(m_handle, targetTimeMicroseconds))) {
        printf("[W] ToupTekCameraManager::setExposureTime failed.\n");
        return false;
    }
    printf("[D] ToupTekCameraManager::setExposureTime set to %.3f ms.\n", float(targetTimeMicroseconds) * 0.001f);

    if (realTimeMicroseconds)
        Toupcam_get_RealExpoTime(m_handle, realTimeMicroseconds);

    return true;
}

const ToupTekCameraManager::RangeDTO& ToupTekCameraManager::getExposureGainRange(bool* p_dtoUpdated)
{
    bool updated = false;
    if (m_needRenewGainRange) {
        m_needRenewGainRange = false;
        updated = true;
        unsigned short _min = 0, _max = 0, _default = 0;
        {
            std::shared_lock lock(m_handleMutex);
            if (m_handle)
                Toupcam_get_ExpoAGainRange(m_handle, &_min, &_max, &_default);
        }
        m_gainRange.min = _min;
        m_gainRange.max = _max;
        m_gainRange.def = _default;
    }
    if (p_dtoUpdated)
        *p_dtoUpdated = updated;
    return m_gainRange;
}

bool ToupTekCameraManager::setExposureGain(
    unsigned targetGain
) const {
    std::unique_lock lock(m_handleMutex);

    if (!m_handle) {
        return false;
    }
    
    if (FAILED(Toupcam_put_ExpoAGain(m_handle, targetGain))) {
        printf("[W] ToupTekCameraManager::setExposureGain failed.\n");
        return false;
    }
    printf("[D] ToupTekCameraManager::setExposureGain set to %u.\n", targetGain);

    return true;
}

LRESULT __stdcall ToupTekCameraManager::WndProcHandler(
    HWND hWnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
) {
    if (MY_TOUPCAM_MSG == msg) {
        switch (wParam) {
        case TOUPCAM_EVENT_IMAGE: {
            //printf("Image\n");
            std::unique_lock lock(m_pullingBitmapMutex[m_pullingBitmapWritePage]);
            
            ToupcamFrameInfoV4 info{};

            {
                std::unique_lock lock(m_handleMutex);

                int optionRaw = -100;
                int eBitDepth = -100;
                int eRGB = -100;
                Toupcam_get_Option(m_handle, TOUPCAM_OPTION_RAW, &optionRaw);
                Toupcam_get_Option(m_handle, TOUPCAM_OPTION_BITDEPTH, &eBitDepth);
                Toupcam_get_Option(m_handle, TOUPCAM_OPTION_RGB, &eRGB);

                int bpp = 0;
                if (optionRaw) {
                    bpp = eBitDepth ? 16 : 8;
                }
                else {
                    // TODO
                    printf("[W] ToupTekCameraManager::WndProcHandler: {TOUPCAM_EVENT_IMAGE} RGB mode is not tested yet.\n");
                    switch (eRGB) {
                    case 0: bpp = 24; break;
                    case 1: bpp = 48; break;
                    case 2: bpp = 32; break;
                    case 3: bpp = 8; break;
                    case 4: bpp = 16; break;
                    case 5: bpp = 64; break;
                    }
                }

                Toupcam_PullImageV4(m_handle, NULL, 0, bpp, 0, &info);
                unsigned width = info.v3.width;
                unsigned height = info.v3.height;
                bool isRaw = (optionRaw != 0);

                unsigned bpp2;
                unsigned fourCC = 0;
                static constexpr unsigned _rggb = MAKEFOURCC('R', 'G', 'G', 'B');
                static constexpr unsigned _bggr = MAKEFOURCC('B', 'G', 'G', 'R');
                static constexpr unsigned _grbg = MAKEFOURCC('G', 'R', 'B', 'G');
                static constexpr unsigned _gbrg = MAKEFOURCC('G', 'B', 'R', 'G');
                //printf("RGGB: %u    BGGR: %u    GRBG: %u    GBRG: %u\n", _rggb, _bggr, _grbg, _gbrg);
                Toupcam_get_RawFormat(m_handle, &fourCC, &bpp2);
                if (bpp != bpp2) {
                    printf("[W] ToupTekCameraManager::WndProcHandler: {TOUPCAM_EVENT_IMAGE} Check BPP.\n");
                }

                // TODO Это, вероятно, костыль. Нужно думать как обойти
                int upsideDown = 0;
                Toupcam_get_Option(m_handle, TOUPCAM_OPTION_UPSIDE_DOWN, &upsideDown);
                if (upsideDown) {
                    switch (fourCC) {
                    case _rggb: fourCC = _bggr; break;
                    case _bggr: fourCC = _rggb; break;
                    case _grbg: fourCC = _gbrg; break;
                    case _gbrg: fourCC = _grbg; break;
                    }
                }
                
                FrameHeader& header = m_pullingBitmapInfo[m_pullingBitmapWritePage];
                BYTE*& data = m_pullingBitmapData[m_pullingBitmapWritePage];
                if (header.isRaw != isRaw || header.bpp != bpp || header.width != width || header.height != height) {
                    if (data) {
                        free(data);
                        data = NULL;
                    }
                }

                header.width = width;
                header.height = height;
                header.fourCC = fourCC;
                header.bpp = bpp;
                header.isRaw = isRaw;
                if (!data) {
                    header.buffer_size = size_t(TDIBWIDTHBYTES(width * bpp)) * height;
                    data = (BYTE*)malloc(header.buffer_size);
                    printf("[D] ToupTekCameraManager::WndProcHandler: {TOUPCAM_EVENT_IMAGE} Allocated %lld bytes\n", header.buffer_size);
                }

                Toupcam_PullImageV4(m_handle, data, 0, bpp, 0, NULL);
                /*{
                    unsigned p11 = data[0];
                    unsigned p12 = data[1];
                    unsigned p21 = data[width];
                    unsigned p22 = data[width+1];
                    int g = 4;
                }*/
            }

            m_pullingBitmapWritePage = 1 - m_pullingBitmapWritePage;
            break;
        }
        case TOUPCAM_EVENT_DISCONNECTED: {
            printf("[D] ToupTekCameraManager::WndProcHandler: {TOUPCAM_EVENT_DISCONNECTED} Connection lost.\n");
            closeDevice();
            break;
        }
        case TOUPCAM_EVENT_EXPOSURE: {
            printf("[D] ToupTekCameraManager::WndProcHandler: {TOUPCAM_EVENT_EXPOSURE} Exposure or gain changed.\n");
            break;
        }
        default:
            printf("[D] ToupTekCameraManager::WndProcHandler: {default} Event %llu.\n", wParam);
        }
        return 0;
    }
    return 0;
}

bool ToupTekCameraManager::isDeviceRunning() const
{
    return m_isRunning;
}

void ToupTekCameraManager::startDevicePulling(
    HWND hWnd
) {
    std::unique_lock lock(m_handleMutex);

    if (!m_handle) {
        m_isRunning = false;
        return;
    }
    if (m_isRunning)
        return;

    HRESULT hr;
    hr = Toupcam_put_AutoExpoEnable(m_handle, 0);
    if (FAILED(hr))
        printf("[W] ToupTekCameraManager::startDevicePulling: Toupcam_put_AutoExpoEnable failed (code: %ld).\n", hr);
    else {
        printf("[W] ToupTekCameraManager::startDevicePulling: Using HARDCODED Toupcam_put_AutoExpoEnable=0.\n");
    }

    Toupcam_put_Option(m_handle, TOUPCAM_OPTION_DEMOSAIC, 0);
    hr = Toupcam_put_Option(m_handle, TOUPCAM_OPTION_RAW, 1);
    if (FAILED(hr)) {
        printf("[E] ToupTekCameraManager::startDevicePulling: Setting TOUPCAM_OPTION_RAW failed (code: %ld).\n", hr);
        closeDevice();
        return;
    }
    printf("[i] ToupTekCameraManager::startDevicePulling: Using HARDCODED TOUPCAM_OPTION_RAW=1.\n");

    hr = Toupcam_StartPullModeWithWndMsg(m_handle, hWnd, MY_TOUPCAM_MSG);
    if (FAILED(hr)) {
        printf("[E] ToupTekCameraManager::startDevicePulling: can't start pulling model with WndMsg (code %ld).\n", hr);
        closeDevice();
        return;
    }
    printf("[D] ToupTekCameraManager::startDevicePulling: start.\n");

    m_isRunning = true;
}

bool ToupTekCameraManager::grabImageData(FrameHeader& header, BYTE*& dst) const
{
    std::shared_lock lock(m_pullingBitmapMutex[1 - m_pullingBitmapWritePage]);
    static size_t prev_page = 1000;
    size_t page = 1 - m_pullingBitmapWritePage;

    if (!m_pullingBitmapData[page])
        return false;

    if (page == prev_page)
        return false;
    prev_page = page;

    if (!dst || header.buffer_size != m_pullingBitmapInfo[page].buffer_size) {
        if (dst) {
            free(dst);
            dst = NULL;
        }
        dst = (BYTE*)malloc(m_pullingBitmapInfo[page].buffer_size);
    }
    header = m_pullingBitmapInfo[page];
    memcpy(dst, m_pullingBitmapData[page], header.buffer_size);

    return true;
}

bool ToupTekCameraManager::debayerRawImage(
    _In_ FrameHeader& headerSrc,
    _In_ BYTE*& dataSrc,
    _Out_ FrameHeader& headerDst,
    _Out_ BYTE*& dataDst
) {
    if (!headerSrc.isRaw)
        return false;

    int width = headerSrc.width;
    int height = headerSrc.height;

    if (width <= 0 || height <= 0) {
        printf("[E] ToupTekCameraManager::debayerRawImage: Check image W&H.\n");
        return false;
    }

    BYTE* rgb;
    size_t alloc_size = 0;
    if (headerSrc.bpp == 8)
        alloc_size = size_t(width) * 3 * height;
    else if (headerSrc.bpp == 16)
        alloc_size = size_t(width) * 3 * 2 * height;
    else {
        printf("[E] ToupTekCameraManager::debayerRawImage: Unsupported bpp %u.\n", headerSrc.bpp);
        return false;
    }

    rgb = (BYTE*)malloc(alloc_size);
    if (!rgb) {
        printf("[E] ToupTekCameraManager::debayerRawImage: Can't malloc %llu bytes.\n", alloc_size);
        return false;
    }
    
    if (headerSrc.bpp == 8)
        Debayer::demosaic<uint8_t>((uint8_t*)dataSrc, width, height, headerSrc.fourCC, (uint8_t*)rgb);
    else
        Debayer::demosaic<uint16_t>((uint16_t*)dataSrc, width, height, headerSrc.fourCC, (uint16_t*)rgb);

    headerDst.width = headerSrc.width;
    headerDst.height = headerSrc.height;
    headerDst.fourCC = 0;
    headerDst.buffer_size = alloc_size;
    headerDst.bpp = headerSrc.bpp == 8 ? 24 : 48;
    headerDst.isRaw = false;
    if (dataDst) {
        free(dataDst);
        dataDst = NULL;
    }
    dataDst = rgb;

    return true;
}

void ToupTekCameraManager::onDeviceOpen(
    HToupcam handle,
    const std::wstring& id
) {
    if (handle) {
        std::unique_lock lock(m_handleMutex);
        std::unique_lock lock2(m_devicesMutex);
        this->m_handle = handle;
        for (size_t i = 0; i < m_devicesCount && m_openedDeviceIdx == TOUPCAM_MAX; i++) {
            if (id == m_devices[i].id)
                m_openedDeviceIdx = i;
        }
    }
    m_pendingHandles--;

    m_needRenewDeviceResolutionsDTO = true;
    m_needRenewPixelFormatsDTO = true;
    m_needRenewExposureTimeRange = true;
    m_needRenewGainRange = true;
}

void ToupTekCameraManager::onUpdateDeviceList(
    int devicesCount,
    ToupcamDeviceV2* devices
) {
    std::unique_lock lock(m_devicesMutex);
    bool list_changed = false;

    list_changed |= (devicesCount != m_devicesCount);

    for (size_t i = 0; i < devicesCount && !list_changed; i++)
        list_changed |= (devices[i].id != m_devices[i].id);

    if (list_changed) {
        m_devicesCount = devicesCount;
        memcpy(m_devices, devices, sizeof(ToupcamDeviceV2) * TOUPCAM_MAX);
    }
    m_needRenewDevicesIdDTO |= list_changed;
}

void ToupTekCameraManager::invalidatePullingBitmap(
    size_t page
) {
    std::unique_lock lock(m_pullingBitmapMutex[page]);
    memset(&m_pullingBitmapInfo[page], 0, sizeof(m_pullingBitmapInfo[page]));
    if (m_pullingBitmapData[page]) {
        free(m_pullingBitmapData[page]);
        m_pullingBitmapData[page] = NULL;
    }
}
