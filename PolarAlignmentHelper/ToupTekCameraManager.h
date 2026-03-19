#pragma once

#include "toupcam.h"
#include <shared_mutex>
#include <string>
#include <set>
#include <map>
#include <vector>
#include "ThreadWorker.h"
#include <wingdi.h>

class ToupTekCameraManager
{
public:

#ifdef _WIN32
#define MY_TOUPCAM_MSG   (WM_USER + 64)
    using char_type = wchar_t;
    using string_type = std::wstring;
#else
    using char_type = char;
    using string_type = std::string;
#endif

    struct DeviceIdDTO {
        string_type displayname;
        string_type id;
    };

    struct PixelFormatDTO {
        std::string name;
        int pixelFormat;
    };

    struct RangeDTO {
        unsigned min;
        unsigned max;
        unsigned def;
    };

    struct FrameHeader {
        unsigned width;
        unsigned height;
        size_t buffer_size;
        unsigned short bpp;
        bool isRaw;
    };

    ToupTekCameraManager(ThreadWorker* worker);
    virtual ~ToupTekCameraManager();

    /// <summary>
    ///		Обновляет список камер (не блокируя текущий поток).
    ///     При подключенном устройстве, вызов данной функции может привести к
    ///     неопределенному поведению / необработанному исключению в прочих методах.
    /// </summary>
    void updateDeviceList();

    /// <summary>
    ///		Возвращает список объектов, содержащий сведения об Id камер и их названиях.
    /// </summary>
    /// <param name="dtoUpdated">Указывает, обновился ли список (для интерфейса).</param>
    /// <returns>
    ///     Список идентификаторов и имён устройств.
    /// </returns>
    const std::vector<DeviceIdDTO>& getDevicesIdDTO(bool* p_dtoUpdated = NULL);

    /// <summary>
    ///		Получить перечисление экземпляра камеры.
    /// </summary>
    /// <param name="idx">Индекс устройства.</param>
    /// <param name="p_success">Сигнализирует об ошибке.</param>
    /// <returns>
    ///		Идентификационные данные устройства.
    /// </returns>
    const ToupcamDeviceV2 getDeviceInfo(int idx, bool* p_success = NULL) const;

    /// <summary>
    ///		Выводит всю доступную информацию о подключенной камере.
    /// </summary>
    /// <param name="idx">Индекс устройства.</param>
    void wprintDeviceInfo(int idx) const;

    /// <summary>
    ///     Проверяет наличие открытого хендла камеры.
    /// </summary>
    /// <returns>
    ///     True, если камера открыта.
    ///     False - в ином случае.
    /// </returns>
    bool isDeviceOpened() const;

    /// <summary>
    ///     Открывает камеру (не блокируя текущий поток).
    ///     При неудаче, новый хендл не создаётся.
    /// </summary>
    /// <param name="id">Идентификатор устройства.</param>
    /// <returns>
    ///     True - если никакая камера до этого не была открыта.
    ///     False - в ином случае.
    /// </returns>
    bool openDevice(const std::wstring& id);

    /// <summary>
    ///     Показывает, ожидается ли открытие камеры.
    /// </summary>
    /// <returns>
    ///     True, если был вызван метод openCamera(...), и операция открытия ещё
    ///     не была выполнена (вне зависимости от успеха).
    ///     False - в ином случае.
    /// </returns>
    bool waitingDeviceToOpen() const;

    /// <summary>
    ///     Метод получения хендла камеры для специфической настройки.
    /// </summary>
    /// <returns>
    ///     Хендл камеры.
    /// </returns>
    HToupcam getDeviceHandle();

    /// <summary>
    ///     Закрывает хендл камеры.
    /// </summary>
    void closeDevice();

    /// <summary>
    ///     Метод получения списка разрешений предпросмотра подключенной камеры.
    /// </summary>
    /// <param name="dtoUpdated">Указывает, обновился ли список (для интерфейса).</param>
    /// <returns>
    ///     Список доступных разрешений предпросмотра. Порядок следования важен.
    ///     В случае, если камера не подключена, возвращает пустой список.
    /// </returns>
    const std::vector<ToupcamResolution>& getPreviewResolutionsDTO(bool *p_dtoUpdated = NULL);

    /// <summary>
    ///     Останавливает камеру и меняет разрешение предпросмотра (не блокируя текущий поток).
    /// </summary>
    /// <param name="resId">Порядковый номер выбранного разрешения.</param>
    /// <returns>
    ///     True - если хендл камеры был открыт.
    ///     False - в ином случае.
    /// </returns>
    bool setCameraPreviewResolution(size_t eSizeTarget);

    /// <summary>
    ///     Метод получения списка поддерживаемых форматов пикселя.
    /// </summary>
    /// <param name="dtoUpdated">Указывает, обновился ли список (для интерфейса).</param>
    /// <returns>
    ///     Список доступных форматов пикселя.
    ///     В случае, если камера не подключена, возвращает пустой список.
    /// </returns>
    const std::vector<PixelFormatDTO>& getPixelFormatsDTO(bool* p_dtoUpdated = NULL);

    /// <summary>
    ///     Устанавливает формат пикселя.
    /// </summary>
    /// <param name="targetPixelFormat">Идентификатор формата пикселя.</param>
    /// <returns>
    ///     True, если формат пикселя удалось установить.
    ///     False - в ином случае.
    /// </returns>
    bool setPixelFormat(int targetPixelFormat);

    /// <summary>
    ///     Метод получения диапазона поддерживаемого времени экспозиции.
    /// </summary>
    /// <param name="dtoUpdated">Указывает, обновился ли объект (для интерфейса).</param>
    /// <returns>
    ///     Диапазон поддерживаемого времени экспозиции.
    ///     В случае, если камера не подключена, возвращает нули.
    /// </returns>
    const RangeDTO& getExposureTimeRange(bool* p_dtoUpdated = NULL);

    /// <summary>
    ///     Устанавливает время экспозиции в микросекундах.
    /// </summary>
    /// <param name="targetTimeMicroseconds">Целевая экспозиция.</param>
    /// <param name="realTimeMicroseconds">Реальная экспозиция (запрошена с камеры).</param>
    /// <returns>
    ///     True, если время экспозиции удалось установить.
    ///     False - в ином случае.
    /// </returns>
    bool setExposureTime(unsigned targetTimeMicroseconds, unsigned* realTimeMicroseconds = NULL) const;

    /// <summary>
    ///     Метод получения диапазона поддерживаемого усиления экспозиции.
    /// </summary>
    /// <param name="dtoUpdated">Указывает, обновился ли объект (для интерфейса).</param>
    /// <returns>
    ///     Диапазон поддерживаемого усиления экспозиции.
    ///     В случае, если камера не подключена, возвращает нули.
    /// </returns>
    const RangeDTO& getExposureGainRange(bool* p_dtoUpdated = NULL);

    /// <summary>
    ///     Устанавливает усиление экспозиции.
    /// </summary>
    /// <param name="targetTimeMicroseconds">Целевое усиление.</param>
    /// <returns>
    ///     True, если усиление экспозиции удалось установить.
    ///     False - в ином случае.
    /// </returns>
    bool setExposureGain(unsigned targetGain) const;

    LRESULT WINAPI WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool isDeviceRunning() const;

    void startDevicePulling(HWND hWnd);
    
    bool grabImageData(FrameHeader& header, BYTE*& dst) const;

protected:
    int                        m_devicesCount;
    mutable std::shared_mutex  m_devicesMutex;
    ToupcamDeviceV2            m_devices[TOUPCAM_MAX];

    volatile size_t            m_pendingHandles;
    size_t                     m_openedDeviceIdx;
    mutable std::shared_mutex  m_handleMutex;
    HToupcam                   m_handle;
    bool                       m_isRunning;

    ThreadWorker*              m_worker;

    // Кэширование
    bool                            m_needRenewDevicesIdDTO;
    std::vector<DeviceIdDTO>        m_devicesIdDTO;
    bool                            m_needRenewDeviceResolutionsDTO;
    std::vector<ToupcamResolution>  m_deviceResolutionsDTO;
    bool                            m_needRenewPixelFormatsDTO;
    std::vector<PixelFormatDTO>     m_pixelFormatsDTO;
    bool                            m_needRenewExposureTimeRange;
    RangeDTO                        m_exposureTimeRange;
    bool                            m_needRenewGainRange;
    RangeDTO                        m_gainRange;


    volatile size_t                m_pullingBitmapWritePage;
    alignas(std::hardware_destructive_interference_size)
        mutable std::shared_mutex  m_pullingBitmapMutex[2];
    FrameHeader                    m_pullingBitmapInfo[2];
    BYTE*                          m_pullingBitmapData[2];


    void onDeviceOpen(HToupcam handle, const std::wstring& id);
    void onUpdateDeviceList(int devicesCount, ToupcamDeviceV2* devices);

    void invalidatePullingBitmap(size_t page);
};
