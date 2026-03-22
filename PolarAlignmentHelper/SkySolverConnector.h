#pragma once

 #include <winsock.h>

#include <string>
#include <shared_mutex>
#include <vector>


// TODO: сделать ThreadWorker на connect и ping
class SkySolverConnector
{
public:

    struct PlatesolveResult {
        double ra;                      // degrees
        double dec;                     // degrees
        double pixel_scale;             // arcsec/pix

        struct FieldSize {
            double x;
            double y;
        } field_size;                   // arcseconds

        double rotation_angle_E_of_N;   // degrees

        bool parity;                    // True -> 'pos', False -> 'neg'

        bool success;                   // Решена ли пластина
        bool valid;                     // Ошибка при парсинге ответа

        std::string error;
    };

    struct PlatesolveDTO {
        double ra;
        double dec;
        double pixel_scale;
        double field_size_x;
        double field_size_y;
        double rotation_angle;
        bool parity;
    };

    SkySolverConnector();
    virtual ~SkySolverConnector();

    void setConnectionAddress(const std::string& ip, int port);

    bool isConnected() const;

    bool startSolve(const std::string& imagePath, size_t timeLimitSec = 0);
    bool stopSolve();
    bool isSolveRunning();
    bool isSolveFinished();

    // true if started
    bool addMountPoint(const std::string& imagePath, size_t timeLimitSec = 0);
    void clearMountPoints();
    bool isMountPointPending();
    std::vector<PlatesolveDTO>& getMountPointsDTO();

    void                    resetLastPlatesolveResult();
    const PlatesolveResult& getLastPlatesolveResult();

protected:
    SOCKET         m_sock;
    volatile bool  m_isConnected;
    std::string    m_serverIp;
    int            m_serverPort;

    std::thread    m_connectPingThr;
    volatile bool  m_connectPingThrWork;

    mutable std::shared_mutex  m_lastPlateSolveResultMutex;
    PlatesolveResult           m_lastPlateSolveResult;
    std::thread*               m_platesolveResultUpdaterThr;
    volatile bool              m_isSolveRunning;
    volatile bool              m_isSolveFinished;

    mutable std::shared_mutex      m_mountPtsMutex;
    volatile bool                  m_mountPtPending;
    std::vector<PlatesolveResult>  m_mountPts;

    // кэширование
    volatile bool               m_mountPtsDTOUpdated;
    std::vector<PlatesolveDTO>  m_mountPtsDTO;

    static bool InitWinsock();
    static void CleanupWinsock();

    bool connect();
    void disconnect();
    bool ping();

    void loopConnectAndPing();

    void loopUpdatePlatesolveResult();

    bool getStatus(std::string& status);
    bool getFailReason(std::string& reason);
    bool getCompletionResult(PlatesolveResult& result);

    static std::vector<std::string> splitArgs(std::string response, char delimiter='|');
    static bool myStoD(const std::string& str, double& d);

};

