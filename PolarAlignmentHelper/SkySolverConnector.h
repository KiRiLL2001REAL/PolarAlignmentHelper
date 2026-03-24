#pragma once

 #include <winsock.h>

#include <string>
#include <shared_mutex>
#include <vector>
#include <filesystem>
#include <mutex>


// TODO: сделать CameraThreadWorker на connect и ping
class SkySolverConnector
{
public:
    SkySolverConnector();
    virtual ~SkySolverConnector();

    void setConnectionAddress(const std::string& ip, int port);

    bool isConnected() const;

    bool getStatus(std::string& status);
    bool getResult(
        double                 & ra,
        double                 & dec,
        double                 & pixel_scale,
        double                 & field_size_x,
        double                 & field_size_y,
        double                 & rotation_angle,
        bool                   & parity,
        bool                   & success,
        std::filesystem::path  & imgPath,
        std::string            & error);

    bool solve(const std::filesystem::path& imagePath, size_t timeLimitSec = 0);
    bool cancel();

protected:
    std::mutex     m_socketMutex;
    SOCKET         m_sock;
    volatile bool  m_isConnected;
    std::string    m_serverIp;
    int            m_serverPort;

    std::thread    m_thr;
    volatile bool  m_thrWork;

    static bool InitWinsock();
    static void CleanupWinsock();

    bool connect();
    void disconnect();
    bool ping();

    bool sendNrecv(const char* msg, char* retBuffer, int retBufferLength, int* pRetBytes);

    void loop();

    static std::vector<std::string> splitArgs(std::string response, char delimiter='|');
    static bool myStoD(const std::string& str, double& d);

};

