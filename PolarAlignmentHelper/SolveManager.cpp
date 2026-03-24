#include "SolveManager.h"

#include <thread>
#include "Utility.h"
#include <exception>

uint16_t SolveManager::m_idCounter = 0;

SolveManager::SolveManager(
    const std::filesystem::path& saveDirectory,
    const std::string& ip,
    int port
) :
    m_idCounterMutex(),

    m_saveImageJpgMutex(),
    m_isImageSaving(false),
    m_fullSavedPaths(),

    m_skySolverTimeLimitSec(120),
    m_skySolver(),

    m_mountPlatesMutex(),
    m_mountPlates(),
    m_mountAdjustingMode(false),

    m_adjustedPlateMutex(),
    m_adjustedPlatePresent(false),
    m_adjustedPlate(),

    m_handlerThreadWorking(false),
    m_handlerThread(),

    m_mountPlatesDTOUpdated(false),
    m_mountPlatesDTO()
{
    setSaveDirectory(saveDirectory);
    setConnectionAddress(ip, port);

    m_handlerThreadWorking = true;
    m_handlerThread = std::thread(&SolveManager::loopHandler, this);
}

SolveManager::~SolveManager()
{
    if (std::filesystem::exists(m_saveDirectory))
    {
        try {
            std::filesystem::remove_all(m_saveDirectory);
        }
        catch (std::exception& e) {
            printf("[W] SolveManager::dtor: std::filesystem exception: (%s).\n", e.what());
        }
    }

    m_handlerThreadWorking = false;
    if (m_handlerThread.joinable())
        m_handlerThread.join();

    m_mountPlatesDTOUpdated = false;
    m_mountPlatesDTO.clear();

    {
        std::scoped_lock lock(m_saveImageJpgMutex);
        std::queue<std::filesystem::path> empty;
        m_fullSavedPaths.swap(empty);
    }
}

bool SolveManager::isServerConnected() const
{
    return m_skySolver.isConnected();
}

void SolveManager::setSaveDirectory(
    const std::filesystem::path& saveDirectory
) {
    namespace fs = std::filesystem;

    if (fs::exists(m_saveDirectory))
        fs::remove_all(m_saveDirectory);

    m_saveDirectory = saveDirectory;
}

void SolveManager::setConnectionAddress(
    const std::string& ip,
    int port
) {
    m_skySolver.setConnectionAddress(ip, port);
}

void SolveManager::setSolvingTimeLimit(
    size_t timeLimitSec
) {
    m_skySolverTimeLimitSec = timeLimitSec;
}

bool SolveManager::isImageSaving() const
{
    return m_isImageSaving;
}

bool SolveManager::solveMountPlate(
    unsigned char* data,
    Imaging::FrameHeader& frameHeader,
    unsigned quality,
    const std::string& imageNamePrefix
) {
    {
        std::shared_lock lock(m_mountPlatesMutex);
        if (m_mountAdjustingMode) {
            printf("[W] SolveManager::solveMountPlate: Axis-adjustment mode.\n");
            return false;
        }
    }
    if (!saveImageJpg(data, frameHeader, quality, imageNamePrefix))
    {
        printf("[W] SolveManager::solveMountPlate: Previous image is still saving.\n");
        return false;
    }

    std::thread thr([&]()
        {
            while (m_isImageSaving) std::this_thread::yield();

            std::unique_lock lock(m_mountPlatesMutex);
            if (m_mountAdjustingMode)
                return;
            CelestialPlate plate;
            {
                std::scoped_lock lock(m_saveImageJpgMutex);
                std::filesystem::path saved_path = m_fullSavedPaths.front();
                m_fullSavedPaths.pop();
                plate.metadata.imagePath = saved_path;
            }
            m_mountPlates.push_back(plate);
            m_mountPlatesDTOUpdated = true;
        }
    );
    thr.detach();

    return true;
}

bool SolveManager::mountAxisCanBeAppoximated() const
{
    size_t count = 0;

    std::shared_lock lock(m_mountPlatesMutex);
    for (size_t i = 0; i < m_mountPlates.size(); i++) {
        auto& plate = m_mountPlates[i];
        if (plate.metadata.processed && plate.metadata.success)
            count++;
    }

    return count >= 3;
}

bool SolveManager::canAddMountPlates() const
{
    std::shared_lock lock(m_mountPlatesMutex);
    return !m_mountAdjustingMode;
}

void SolveManager::clearMountPlates()
{
    std::unique_lock lock(m_mountPlatesMutex);
    for (size_t i = 0; i < m_mountPlates.size(); i++) {
        CelestialPlate& plate = m_mountPlates[i];
        if (plate.metadata.solving) {
            m_skySolver.cancel();
            break;
        }
    }
    m_mountPlates.clear();
    if (m_mountAdjustingMode) {
        printf("[D] SolveManager::clearMountPlates: Returning to mount axis-approximation mode.\n");
        std::unique_lock lock(m_adjustedPlateMutex);
        m_adjustedPlate = {};
    }
    m_mountAdjustingMode = false;

    m_mountPlatesDTOUpdated = true;
}

const std::vector<SolveManager::CelestialPlateDTO>& SolveManager::getMountPlatesDTO(
    bool* p_dtoUpdated
) {
    bool updated = false;
    if (m_mountPlatesDTOUpdated) {
        m_mountPlatesDTOUpdated = false;
        updated = true;
        m_mountPlatesDTO.clear();
        {
            std::shared_lock lock(m_mountPlatesMutex);
            for (size_t i = 0; i < m_mountPlates.size(); i++) {
                CelestialPlate& plate = m_mountPlates[i];
                CelestialPlateDTO dto;
                dto.ra = plate.ra;
                dto.dec = plate.dec;
                dto.pixel_scale = plate.pixel_scale;
                dto.field_size_x = plate.field_size_x;
                dto.field_size_y = plate.field_size_y;
                dto.rotation_angle = plate.rotation_angle;
                dto.parity = plate.parity;
                dto.solving = plate.metadata.solving;
                dto.processed = plate.metadata.processed;
                dto.success = plate.metadata.success;
                dto.error = plate.metadata.error;
                m_mountPlatesDTO.push_back(dto);
            }
        }
    }
    if (p_dtoUpdated)
        *p_dtoUpdated = updated;
    return m_mountPlatesDTO;
}

bool SolveManager::startMountAltAzAdjusting()
{
    bool ok = mountAxisCanBeAppoximated();
    if (ok) {
        std::unique_lock lock(m_mountPlatesMutex);
        m_mountAdjustingMode = true;
        printf("[D] SolveManager::startMountAltAzAdjusting: Entered mount axis-adjustment mode.\n");
    }
    return ok;
}

bool SolveManager::solveAltAzAdjustedPlate(
    unsigned char* data,
    Imaging::FrameHeader& frameHeader,
    unsigned quality,
    const std::string& imageNamePrefix
) {
    {
        std::shared_lock lock(m_mountPlatesMutex);
        if (!m_mountAdjustingMode) {
            printf("[W] SolveManager::solveAltAzAdjustedPlate: Axis-approximation mode.\n");
            return false;
        }
    }
    {
        std::shared_lock lock(m_adjustedPlateMutex);
        if (m_adjustedPlatePresent && m_adjustedPlate.metadata.solving)
        {
            printf("[W] SolveManager::solveAltAzAdjustedPlate: Previous plate is still in processing.\n");
            return false;
        }
    }
    if (!saveImageJpg(data, frameHeader, quality, imageNamePrefix))
    {
        printf("[W] SolveManager::solveAltAzAdjustedPlate: Previous image is still saving.\n");
        return false;
    }

    std::thread thr([&]()
        {
            while (m_isImageSaving) std::this_thread::yield();

            {
                std::shared_lock lock(m_mountPlatesMutex);
                if (!m_mountAdjustingMode)
                    return;
                // TODO потенциальная проблема с перенасыщением m_fullSavedPaths
            }
            CelestialPlate plate;
            {
                std::scoped_lock lock(m_saveImageJpgMutex);
                std::filesystem::path saved_path = m_fullSavedPaths.front();
                m_fullSavedPaths.pop();
                plate.metadata.imagePath = saved_path;
            }

            std::unique_lock lock(m_adjustedPlateMutex);
            m_adjustedPlate = plate;
            m_adjustedPlatePresent = true;
        }
    );
    thr.detach();

    return true;
}

bool SolveManager::saveImageJpg(
    unsigned char* data,
    Imaging::FrameHeader& frameHeader,
    unsigned quality,
    const std::string& imageNamePrefix
) {
    if (m_isImageSaving)
        return false;
    std::scoped_lock lock(m_saveImageJpgMutex);
    if (m_isImageSaving)
        return false;

    m_isImageSaving = true;

    std::string image_name = imageNamePrefix + "_" + nextId() + ".jpg";

    static std::filesystem::path filename;
    static Imaging::FrameHeader header_copy;
    static unsigned char* buffer_copy;

    filename = m_saveDirectory / image_name;
    header_copy = frameHeader;
    buffer_copy = (unsigned char*)malloc(header_copy.buffer_size);

    if (!buffer_copy) {
        printf("[X] SolveManager::saveImageJpg: Can't allocate %llu bytes.\n", header_copy.buffer_size);
        m_isImageSaving = false;
        return false;
    }
    memcpy(buffer_copy, data, header_copy.buffer_size);

    std::thread thr([&]()
        {
            std::filesystem::path imageFullPath;
            bool ok = Utility::writeRGBJpeg(buffer_copy, header_copy.width, header_copy.height, quality, filename, imageFullPath);

            if (ok) {
                std::scoped_lock lock(m_saveImageJpgMutex);
                m_fullSavedPaths.push(imageFullPath);
            }

            memset(&header_copy, 0, sizeof(header_copy));
            free(buffer_copy);
            m_isImageSaving = false;

            std::string s = imageFullPath.string();
            printf("[D] SolveManager::saveImageJpg: saved \"%s\".\n", s.c_str());
        }
    );
    thr.detach();

    return true;
}

std::string SolveManager::nextId()
{
    std::scoped_lock lock(m_idCounterMutex);
    
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%0*ld", 4, m_idCounter);
    m_idCounter++;

    return std::string(buffer);
}

void SolveManager::loopHandler()
{
    printf("[D] SolveManager::loopHandler: START.\n");
    static const auto CHECKING_INTERVAL = std::chrono::nanoseconds(500'000'000);  // 500ms
    std::chrono::high_resolution_clock::time_point last_checkout;

    while (m_handlerThreadWorking) {
        last_checkout = std::chrono::high_resolution_clock::now();

        // Сначала проверяем, не идет ли обработка какой-то пластины из набора монтировки (vector).
        // Если идёт    -> ожидаем решение, и получаем его.
        // Если не идёт -> ищем нерешенную...
        //                 Если нашли    -> запускаем процесс решения.
        //                 Если не нашли -> проверяем, не запросил ли пользователь решение текущей пластины (struct)...
        //                                  Если не запросил -> ничего не делаем.
        //                                  Если запросил    -> Пластина решается?
        //                                                      Да  -> ожидаем решение, и получаем его.
        //                                                      Нет -> запускаем процесс решения.

        bool is_mount_plate_solving = false;
        size_t solving_mount_plate_idx = 0;
        {
            std::shared_lock lock(m_mountPlatesMutex);
            for (size_t i = 0; i < m_mountPlates.size() && !is_mount_plate_solving; i++) {
                if (m_mountPlates[i].metadata.solving) {
                    is_mount_plate_solving = true;
                    solving_mount_plate_idx = i;
                }
            }
        }
        // Если идёт обработка пластины -> ожидаем решение, и получаем его.
        if (is_mount_plate_solving) {
            bool answerWontBeRetrieved = false;
            handleSkySolverStatus(m_mountPlatesMutex, m_mountPlates[solving_mount_plate_idx], answerWontBeRetrieved, &m_mountPlatesDTOUpdated);
            if (answerWontBeRetrieved)
            {
                std::unique_lock lock(m_mountPlatesMutex);
                m_mountPlates.erase(m_mountPlates.begin() + solving_mount_plate_idx);
            }
        }
        else {
            // Если обработка пластины не идёт -> ищем нерешенную...
            bool is_mount_plate_pending = false;
            size_t pending_mount_plate_idx = 0;
            {
                std::shared_lock lock(m_mountPlatesMutex);
                for (size_t i = 0; i < m_mountPlates.size() && !is_mount_plate_pending; i++) {
                    if (!m_mountPlates[i].metadata.processed) {
                        is_mount_plate_pending = true;
                        pending_mount_plate_idx = i;
                    }
                }
            }
            // Если нашли нерешенную пластину -> запускаем процесс решения.
            if (is_mount_plate_pending) {
                {
                    std::unique_lock lock(m_mountPlatesMutex);
                    if (!m_skySolver.solve(m_mountPlates[pending_mount_plate_idx].metadata.imagePath, m_skySolverTimeLimitSec))
                    {
                        // Предыдущее изображение ещё сохраняется, либо сервер не отвечает.
                        // Просто ожидаем.
                    }
                    else
                    {
                        // Взводим флаг начала решения.
                        m_mountPlates[pending_mount_plate_idx].metadata.solving = true;
                        m_mountPlatesDTOUpdated = true;
                    }
                }
            }
            else if (m_mountAdjustingMode && m_adjustedPlatePresent)
            {
                // Проверяем, не запросил ли пользователь решение текущей пластины

                bool is_adjusted_plate_solving;
                {
                    std::shared_lock lock(m_adjustedPlateMutex);
                    is_adjusted_plate_solving = m_adjustedPlate.metadata.solving;
                }
                // Если идёт обработка adjustment-пластины -> ожидаем решение, и получаем его.
                if (is_adjusted_plate_solving)
                {
                    bool answerWontBeRetrieved = false;
                    handleSkySolverStatus(m_adjustedPlateMutex, m_adjustedPlate, answerWontBeRetrieved, NULL);
                    if (answerWontBeRetrieved)
                    {
                        std::unique_lock lock(m_adjustedPlateMutex);
                        m_adjustedPlatePresent = false;
                    }
                }
                else
                {
                    // Обработка adjustment-пластины не идёт -> запускаем процесс решения.
                    std::unique_lock lock(m_adjustedPlateMutex);
                    if (!m_skySolver.solve(m_adjustedPlate.metadata.imagePath, m_skySolverTimeLimitSec))
                    {
                        // Предыдущее изображение ещё сохраняется, либо сервер не отвечает.
                        // Просто ожидаем.
                    }
                    else
                    {
                        // Взводим флаг начала решения.
                        m_adjustedPlate.metadata.solving = true;
                    }
                }
            }
        }

        auto elapsed = std::chrono::high_resolution_clock::now() - last_checkout;
        if (elapsed >= CHECKING_INTERVAL) continue;
        std::this_thread::sleep_for(std::chrono::nanoseconds(CHECKING_INTERVAL - elapsed));
    }
    printf("[D] SolveManager::loopHandler: END.\n");
}

void SolveManager::handleSkySolverStatus(
    std::shared_mutex& refMutex,
    CelestialPlate& refPlate,
    bool& refAnswerWontBeRetrieved,
    bool* pMountPlatesDtoUpdated
) {

    // Логика не совсем правильная. Нужно чекать сервер, и менять свои данные под него.
    // Сейчас, при ответе сервера, imagePath сравнивается с refPlate
    // и при их различии refPlate выбрасывется.

    std::string status;
    if (!m_skySolver.getStatus(status)) {
        // Сервер не отвечает. Решение пластины получено не будет, мы снимаем с неё флаг solving "до лучших времён".
        printf("[W] SolveManager::handleSkySolverStatus: Server unreachable {1}.\n");
        std::unique_lock lock(refMutex);
        refPlate.metadata.solving = false;
        if (pMountPlatesDtoUpdated) *pMountPlatesDtoUpdated = true;
    }
    else {
        // Обрабатываем результат опроса статуса сервера.
        if ("idle" == status)
        {
            // Сервер простаивает. Ситуация может возникнуть, если перезапустить сервер в период, пока данный поток спит.
            // Решение пластины получено не будет, мы снимаем с неё флаг solving "до лучших времён".
            printf("[W] SolveManager::handleSkySolverStatus: Server in IDLE state.\n");
            std::unique_lock lock(refMutex);
            refPlate.metadata.solving = false;
            if (pMountPlatesDtoUpdated) *pMountPlatesDtoUpdated = true;
        }
        else if ("initializing" == status || "running" == status)
        {
            // Ожидаем.
        }
        else if ("cancelled" == status)
        {
            // Пользователь отменил решение. Мы должны убрать эту пластину из обработки.
            refAnswerWontBeRetrieved = true;
            if (pMountPlatesDtoUpdated) *pMountPlatesDtoUpdated = true;
        }
        else if ("completed" == status || "failed" == status)
        {
            // Сервер завершил плейтсолвинг, нам нужно запросить результат.
            CelestialPlate res_plate;

            if (m_skySolver.getResult(
                res_plate.ra,
                res_plate.dec,
                res_plate.pixel_scale,
                res_plate.field_size_x,
                res_plate.field_size_y,
                res_plate.rotation_angle,
                res_plate.parity,
                res_plate.metadata.success,
                res_plate.metadata.imagePath,
                res_plate.metadata.error))
            {
                // Поскольку данный поток единолично ходит в SkySolverConnector, варианта,
                // когда imagePath отличается, быть не может. Но на всякий случай стоит убедиться в этом.
                std::unique_lock lock(refMutex);

                std::string actualImagePathStr = refPlate.metadata.imagePath.u8string();
                std::string returnedImagePathStr = refPlate.metadata.imagePath.u8string();
                if (returnedImagePathStr.find(actualImagePathStr) != std::string::npos)
                {
                    res_plate.metadata.solving = false;
                    res_plate.metadata.processed = true;
                    if ("completed" == status)
                        res_plate.metadata.success = true;
                    refPlate = res_plate;
                }
                else
                {
                    // imagePath отличается. Мы должны убрать эту пластину из обработки.
                    printf("[W] SolveManager::handleSkySolverStatus: Different imagePath. Drop.\n");
                    refAnswerWontBeRetrieved = true;
                }
                if (pMountPlatesDtoUpdated) *pMountPlatesDtoUpdated = true;
            }
            else
            {
                // Сервер не отвечает. Решение пластины получено не будет, мы снимаем с неё флаг solving "до лучших времён".
                printf("[W] SolveManager::handleSkySolverStatus: Server unreachable {2}.\n");
                std::unique_lock lock(refMutex);
                refPlate.metadata.solving = false;
                if (pMountPlatesDtoUpdated) *pMountPlatesDtoUpdated = true;
            }
        }
        else
        {
            // Неизвестный статус. Кидаем исключение.
            printf("[E] SolveManager::handleSkySolverStatus: Unknown Status (%s).\n", status.c_str());
            throw std::exception("SolveManager::handleSkySolverStatus: Unknown Status");
        }
    }
}

SolveManager::CelestialPlate::CelestialPlate():
    ra(0.0),
    dec(0.0),
    pixel_scale(0.0),
    field_size_x(0.0),
    field_size_y(0.0),
    rotation_angle(0.0),
    parity(false),
    metadata()
{
}

SolveManager::CelestialPlate::Metadata::Metadata():
    solving(false),
    processed(false),
    success(false),
    imagePath(""),
    error("")
{
}

SolveManager::CelestialPlateDTO::CelestialPlateDTO():
    ra(0.0),
    dec(0.0),
    pixel_scale(0.0),
    field_size_x(0.0),
    field_size_y(0.0),
    rotation_angle(0.0),
    parity(false),
    solving(false),
    processed(false),
    success(false),
    error("")
{
}
