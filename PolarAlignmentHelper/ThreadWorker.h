#pragma once

#include <thread>
#include <mutex>
#include <toupcam.h>
#include <queue>
#include <functional>
#include <shared_mutex>

class ThreadWorker
{
public:
    enum class TASK_ID {
        none,
        updateDeviceList,
        openDevice,
        setPreviewResolution
    };

    struct TASK_META {
        union {
            struct NONE {
                char unused;
            } none;
            struct OPEN_DEVICE {
#ifdef _WIN32
                wchar_t id[64];
#else
                char_t id[64];
#endif
            } openDevice;
            struct SET_PREVIEW_RESOLUTION {
                std::shared_mutex* p_handleMutex;
                HToupcam* p_handle;
                unsigned eSizeTarget;
            } setPreviewResolution;
        };
    };

    using CallbackDeviceListUpdate = std::function<void(int count, ToupcamDeviceV2* devices)>;
#ifdef _WIN32
    using CallbackDeviceOpen = std::function<void(HToupCam handle, const std::wstring& id)>;
#else
    using CallbackDeviceOpen = std::function<void(HToupCam handle, const std::string& id)>;
#endif
    using CallbackPreviewResolutionSet = std::function<void()>;

    ThreadWorker();
    virtual ~ThreadWorker();

    void start();
    bool deviceInUse() const;
    void stop();

    void addTask(TASK_ID task, TASK_META taskMeta);

    void setCallbackUpdateDeviceList(CallbackDeviceListUpdate callback);
    void setCallbackDeviceOpen(CallbackDeviceOpen callback);
    void setCallbackPreviewResolutionSet(CallbackPreviewResolutionSet callback);

protected:

    std::thread thr;
    volatile bool work;
    volatile bool performingOperationWithDevice;

    std::mutex taskQueueMutex;
    std::queue<std::pair<TASK_ID, TASK_META>> taskQueue;

    CallbackDeviceListUpdate     callbackDeviceListUpdate;
    CallbackDeviceOpen           callbackDeviceOpen;
    CallbackPreviewResolutionSet callbackPreviewResolutionSet;

    std::pair<TASK_ID, TASK_META> popTask();
    void dropTasks();

    void loop();

};

