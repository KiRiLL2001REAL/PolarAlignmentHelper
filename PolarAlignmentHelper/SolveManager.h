#pragma once

#include <vector>
#include <string>
#include <filesystem>
#include <mutex>
#include <shared_mutex>
#include "FrameHeader.h"
#include "SkySolverConnector.h"
#include <thread>
#include <queue>

class SolveManager
{
public:

    struct CelestialPlate {
        double ra;
        double dec;
        double pixel_scale;
        double field_size_x;
        double field_size_y;
        double rotation_angle;
        bool parity;
        
        struct Metadata {
            bool solving;    // Решается сейчас
            bool processed;  // Обработано ли сервером
            bool success;    // Получен ли положительный ответ от сервера

            std::filesystem::path imagePath;
            std::string error;

            Metadata();
        } metadata;

        CelestialPlate();
    };

    struct CelestialPlateDTO {
        double ra;
        double dec;
        double pixel_scale;
        double field_size_x;
        double field_size_y;
        double rotation_angle;
        bool parity;
        bool solving;
        bool processed;
        bool success;
        std::string error;

        CelestialPlateDTO();
    };

    SolveManager(const std::filesystem::path& saveDirectory = "", const std::string& ip = "", int port = 0);
    virtual ~SolveManager();

    bool isServerConnected() const;

    void setSaveDirectory(const std::filesystem::path& saveDirectory);
    void setConnectionAddress(const std::string& ip, int port);
    // TODO std::chrono::seconds
    void setSolvingTimeLimit(size_t timeLimitSec);

    bool isImageSaving() const;
    bool solveMountPlate(unsigned char* data, Imaging::FrameHeader& frameHeader, unsigned quality, const std::string& imageNamePrefix);
    bool mountAxisCanBeAppoximated() const;
    bool canAddMountPlates() const;
    void clearMountPlates();

    const std::vector<CelestialPlateDTO>& getMountPlatesDTO(bool* p_dtoUpdated = NULL);

    bool startMountAltAzAdjusting();

    bool solveAltAzAdjustedPlate(unsigned char* data, Imaging::FrameHeader& frameHeader, unsigned quality, const std::string& imageNamePrefix);

protected:

    std::filesystem::path m_saveDirectory;

    std::mutex       m_idCounterMutex;
    static uint16_t  m_idCounter;

    std::mutex                         m_saveImageJpgMutex;
    volatile bool                      m_isImageSaving;
    std::queue<std::filesystem::path>  m_fullSavedPaths;

    size_t              m_skySolverTimeLimitSec;
    SkySolverConnector  m_skySolver;

    mutable std::shared_mutex    m_mountPlatesMutex;
    std::vector<CelestialPlate>  m_mountPlates;
    volatile bool                m_mountAdjustingMode;

    mutable std::shared_mutex  m_adjustedPlateMutex;
    volatile bool              m_adjustedPlatePresent;
    CelestialPlate             m_adjustedPlate;

    volatile bool  m_handlerThreadWorking;
    std::thread    m_handlerThread;


    // Кэширование
    bool                            m_mountPlatesDTOUpdated;
    std::vector<CelestialPlateDTO>  m_mountPlatesDTO;


    bool saveImageJpg(unsigned char* data, Imaging::FrameHeader& frameHeader, unsigned quality, const std::string& imageNamePrefix);
    std::string nextId();

    void loopHandler();
    void handleSkySolverStatus(std::shared_mutex& mutex, CelestialPlate& plate, bool& answerWontBeRetrieved, bool* pMountPlatesDtoUpdated);

};

