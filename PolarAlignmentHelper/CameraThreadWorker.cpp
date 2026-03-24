#include "CameraThreadWorker.h"

#include <chrono>
#include <cstdio>

CameraThreadWorker::CameraThreadWorker() :
    thr(),
    work(false),
    performingOperationWithDevice(false),
    taskQueueMutex(),
    taskQueue(),
    callbackDeviceListUpdate(nullptr),
    callbackDeviceOpen      (nullptr)
{
}

CameraThreadWorker::~CameraThreadWorker()
{
    stop();
}

void CameraThreadWorker::start()
{
    work = true;
    thr = std::thread(&CameraThreadWorker::loop, this);
}

bool CameraThreadWorker::deviceInUse() const
{
    return performingOperationWithDevice;
}

void CameraThreadWorker::stop()
{
    work = false;
    if (thr.joinable())
        thr.join();
}

void CameraThreadWorker::addTask(TASK_ID task, TASK_META taskMeta)
{
    std::scoped_lock<std::mutex> lock(taskQueueMutex);
    taskQueue.emplace(task, taskMeta);
}

void CameraThreadWorker::setCallbackUpdateDeviceList(CallbackDeviceListUpdate callback)
{
    callbackDeviceListUpdate = callback;
}

void CameraThreadWorker::setCallbackDeviceOpen(CallbackDeviceOpen callback)
{
    callbackDeviceOpen = callback;
}

void CameraThreadWorker::setCallbackPreviewResolutionSet(CallbackPreviewResolutionSet callback)
{
    callbackPreviewResolutionSet = callback;
}

std::pair<CameraThreadWorker::TASK_ID, CameraThreadWorker::TASK_META> CameraThreadWorker::popTask()
{
    std::scoped_lock<std::mutex> lock(taskQueueMutex);
    if (taskQueue.empty()) {
        return std::make_pair(TASK_ID::none, TASK_META());
    }
    auto &front = taskQueue.front();
    TASK_ID taskId = front.first;
    TASK_META taskMeta = front.second;
    taskQueue.pop();
    return std::make_pair(taskId, taskMeta);
}

void CameraThreadWorker::dropTasks()
{
    std::scoped_lock<std::mutex> lock(taskQueueMutex);
    std::queue<std::pair<TASK_ID, TASK_META>> empty;
    std::swap(taskQueue, empty);
}

void CameraThreadWorker::loop()
{
    printf("[D] CameraThreadWorker::loop: START.\n");
    while (work) {
        if (taskQueue.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            continue;
        }

        auto [taskId, taskMeta] = popTask();

        performingOperationWithDevice = true;
        switch (taskId)
        {
        case TASK_ID::updateDeviceList: {
            printf("[D] CameraThreadWorker::loop: {updateDeviceList}.\n");
            ToupcamDeviceV2* devices = (ToupcamDeviceV2*)malloc(sizeof(ToupcamDeviceV2) * TOUPCAM_MAX);
            if (NULL == devices)
                printf("[X] CameraThreadWorker::loop: {updateDeviceList} cannot allocate %lldbytes.\n", sizeof(ToupcamDeviceV2) * TOUPCAM_MAX);
            else {
                int count = Toupcam_EnumV2(devices);
                if (work) {
                    if (callbackDeviceListUpdate)
                        callbackDeviceListUpdate(count, devices);
                    else
                        printf("[E] CameraThreadWorker::loop: {updateDeviceList} callbackUpdateDeviceList isn't set.\n");
                }
                free(devices);
                devices = NULL;
            }
            break;
        }
        case TASK_ID::openDevice: {
            printf("[D] CameraThreadWorker::loop: {openDevice} ");
#ifdef _WIN32
            wprintf(L"%ls", taskMeta.openDevice.id);
            thread_local std::wstring id;
#else
            printf(L"%s", taskMeta.openCamera.id);
            thread_local std::string id;
#endif
            printf("\n");

            id = taskMeta.openDevice.id;

            HToupCam handle = Toupcam_Open(id.c_str());
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (work) {
                if (callbackDeviceOpen)
                    callbackDeviceOpen(handle, id);
                else
                    printf("[E] CameraThreadWorker::loop: {openDevice} cameraOpenCallback isn't set.\n");
            }
            break;
        }
        case TASK_ID::setPreviewResolution: {            
            std::shared_mutex& handleMutex = *taskMeta.setPreviewResolution.p_handleMutex;
            HToupcam& handle = *taskMeta.setPreviewResolution.p_handle;
            unsigned eSizeTarget = taskMeta.setPreviewResolution.eSizeTarget;

            std::unique_lock lock(handleMutex);
            if (!handle)
                printf("[W] CameraThreadWorker::loop: {setPreviewResolution} Handle isn't opened.\n");
            else {
                unsigned eSize;
                HRESULT hr = Toupcam_get_eSize(handle, &eSize);
                if (FAILED(hr))
                    printf("[E] CameraThreadWorker::loop: {setPreviewResolution} Toupcam_get_eSize failed (code: %ld).\n", hr);
                else {
                    if (eSize == eSizeTarget)
                        printf("[D] CameraThreadWorker::loop: {setPreviewResolution} Already satisfied.\n");
                    else {
                        hr = Toupcam_Stop(handle);
                        printf("[D] CameraThreadWorker::loop: {setPreviewResolution} Camera stopped.\n");
                        if (FAILED(hr))
                            printf("[E] CameraThreadWorker::loop: {setPreviewResolution} Toupcam_Stop failed (code: %ld).\n", hr);
                        else {
                            Toupcam_put_eSize(handle, eSizeTarget);
                            printf("[D] CameraThreadWorker::loop: {setPreviewResolution} ok (%u).\n", eSizeTarget);
                            if (callbackPreviewResolutionSet)
                                callbackPreviewResolutionSet();
                            else
                                printf("[E] CameraThreadWorker::loop: {setPreviewResolution} previewResolutionSetCallback isn't set.\n");
                        }
                    }
                }
            }
                
            break;
        }
        // case ...: {
        default:
            break;
        }
        performingOperationWithDevice = false;
    }
    printf("[D] CameraThreadWorker::loop: END.\n");
}
