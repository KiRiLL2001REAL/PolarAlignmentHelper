#include "SkySolverConnector.h"

#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <exception>

SkySolverConnector::SkySolverConnector() :
    m_sock(INVALID_SOCKET),
    m_isConnected(false),
    m_serverIp(""),
    m_serverPort(0),

    m_pThrUpdater(NULL),
    m_isSolveRunning(false),
    m_isSolveFinished(false),
    
    m_mountPtPending(false),
    m_mountPts(),

    m_mountPtsDTOUpdated(false),
    m_mountPtsDTO()
{
    resetLastPlatesolveResult();
}

SkySolverConnector::~SkySolverConnector()
{
    {
        std::unique_lock lock(m_lastPlateSolveResultMutex);
        m_isSolveRunning = false;
        if (m_pThrUpdater)
            if (m_pThrUpdater->joinable())
                m_pThrUpdater->join();
        delete m_pThrUpdater;
        m_pThrUpdater = NULL;
    }

    disconnect();
    resetLastPlatesolveResult();
}

bool SkySolverConnector::InitWinsock()
{
    static WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(1, 1), &wsaData);

    switch (result) {
    case WSASYSNOTREADY: {
        printf("[E] SkySolverConnector::InitWinsock: The underlying network subsystem is not ready for network communication.\n");
        break;
    }
    case WSAVERNOTSUPPORTED: {
        printf("[E] SkySolverConnector::InitWinsock: The version of Windows Sockets support requested is not provided by this particular Windows Sockets implementation.\n");
        break;
    }
    case WSAEINPROGRESS: {
        printf("[E] SkySolverConnector::InitWinsock: A blocking Windows Sockets 1.1 operation is in progress.\n");
        break;
    }
    case WSAEPROCLIM: {
        printf("[E] SkySolverConnector::InitWinsock: A limit on the number of tasks supported by the Windows Sockets implementation has been reached.\n");
        break;
    }
    case WSAEFAULT: {
        printf("[E] SkySolverConnector::InitWinsock: The lpWSAData parameter is not a valid pointer.\n");
        break;
    }
    }

    if (S_OK == result)
        printf("[i] SkySolverConnector::InitWinsock: Success.\n");

    return S_OK == result;
}

void SkySolverConnector::CleanupWinsock()
{
    WSACleanup();
    printf("[i] SkySolverConnector::CleanupWinsock: WSACleanup.\n");
}

bool SkySolverConnector::isConnected() const
{
    return m_isConnected;
}

bool SkySolverConnector::connect(
    const std::string& ip,
    int port
) {
    if (m_isConnected)
        disconnect();

    m_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (INVALID_SOCKET == m_sock) {
        printf("[E] SkySolverConnector::connect: Socket initialization error (code: %d).\n", WSAGetLastError());
        return false;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(ip.c_str());

    int result = ::connect(m_sock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (SOCKET_ERROR == result) {
        printf("[E] SkySolverConnector::connect: Connection failed (code: %d).\n", WSAGetLastError());
        closesocket(m_sock);
        return false;
    }

    m_isConnected = true;
    m_serverIp = ip;
    m_serverPort = port;
    printf("[i] SkySolverConnector::connect: Connection established (%s:%d).\n", ip.c_str(), port);

    return true;
}

void SkySolverConnector::disconnect()
{
    if (m_isConnected) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        m_serverIp = "";
        m_serverPort = 0;
        m_isConnected = false;
    }
}

bool SkySolverConnector::ping()
{
    if (!m_isConnected)
        return false;

    const char* msg = "PING";
    int result = send(m_sock, msg, (int)strlen(msg), 0);
    if (SOCKET_ERROR == result) {
        disconnect();
        return false;
    }

    char buffer[1024] = { 0 };
    int bytes = recv(m_sock, buffer, sizeof(buffer), 0);

    std::string response;
    if (bytes > 0) {
        response = std::string(buffer, bytes);
        if ("PONG" != response) {
            printf("[W] SkySolverConnector::ping: Strange server anwser (%s).\n", response.c_str());
            return false;
        }
    }
    else if (bytes <= 0) {
        disconnect();
        return false;
    }

    return true;
}

bool SkySolverConnector::startSolve(
    const std::string& imagePath,
    size_t timeLimitSec
) {
    if (!m_isConnected)
        return false;

    // преобразование пути Windows -> Linux
    std::string image_path_linux;
    {
        namespace fs = std::filesystem;
        fs::path p_filename = fs::weakly_canonical(fs::path(imagePath));
        image_path_linux = p_filename.u8string();
        std::replace(image_path_linux.begin(), image_path_linux.end(), '\\', '/');
        image_path_linux.erase(image_path_linux.begin() + 1);
        image_path_linux[0] = std::tolower(image_path_linux[0]);
        image_path_linux = "/mnt/" + image_path_linux;
    }

    std::string cmd = "SOLVE:" + image_path_linux;
    if (timeLimitSec > 0)
        cmd += "|LIMIT:" + std::to_string(timeLimitSec);
    int result = send(m_sock, cmd.c_str(), (int)cmd.length(), 0);
    if (SOCKET_ERROR == result) {
        disconnect();
        return false;
    }

    char buffer[1024] = { 0 };
    int bytes = recv(m_sock, buffer, sizeof(buffer), 0);
    if (bytes <= 0) {
        disconnect();
        return false;
    }

    std::string response(buffer, bytes);
    return response.find("OK:solve_started") != std::string::npos;
}

bool SkySolverConnector::stopSolve()
{
    if (!m_isConnected)
        return false;

    const char* msg = "CANCEL";
    int result = send(m_sock, msg, (int)strlen(msg), 0);
    if (SOCKET_ERROR == result) {
        disconnect();
        return false;
    }

    char buffer[1024] = { 0 };
    int bytes = recv(m_sock, buffer, sizeof(buffer), 0);
    if (bytes <= 0) {
        disconnect();
        return false;
    }

    std::string response(buffer, bytes);
    return response.find("OK:") != std::string::npos;
}

bool SkySolverConnector::isSolveRunning()
{
    std::shared_lock lock(m_lastPlateSolveResultMutex);
    return m_isSolveRunning;
}

bool SkySolverConnector::isSolveFinished()
{
    std::shared_lock lock(m_lastPlateSolveResultMutex);
    return m_isSolveFinished;
}

bool SkySolverConnector::addMountPoint(
    const std::string& imagePath,
    size_t timeLimitSec
) {
    {
        std::shared_lock lock(m_lastPlateSolveResultMutex);
        if (m_isSolveRunning)
            return false;
    }

    bool started = startSolve(imagePath, timeLimitSec);

    if (!m_mountPtPending) {
        {
            std::unique_lock lock(m_mountPtsMutex);
            m_mountPtPending = true;
        }

        {
            std::unique_lock lock(m_lastPlateSolveResultMutex);
            m_isSolveRunning = true;
            m_isSolveFinished = false;
        }

        if (m_pThrUpdater)
            throw std::exception("m_pThrUpdater is running");
        m_pThrUpdater = new std::thread(&SkySolverConnector::loopUpdatePlatesolveResult, this);

        std::thread thr([&]()
            {
                bool working;
                do {
                    std::this_thread::sleep_for(std::chrono::milliseconds(25));
                    std::shared_lock lock(m_lastPlateSolveResultMutex);
                    working = m_isSolveRunning;
                } while (working);

                if (isSolveFinished()) {
                    PlatesolveResult result = getLastPlatesolveResult();
                    if (!result.valid || !result.error.empty())
                        printf("[E] SkySolverConnector::addMountPoint(thr): error (%s).\n", result.error.c_str());
                    else if (result.success) {
                        std::unique_lock lock(m_mountPtsMutex);
                        m_mountPts.push_back(result);
                        m_mountPtsDTOUpdated = true;
                    }
                }
                else
                    printf("[W] SkySolverConnector::addMountPoint(thr): something goes wrong.\n");
                {
                    std::unique_lock lock(m_mountPtsMutex);
                    m_mountPtPending = false;
                }

                {
                    std::unique_lock lock(m_lastPlateSolveResultMutex);
                    if (m_pThrUpdater)
                        if (m_pThrUpdater->joinable())
                            m_pThrUpdater->join();
                    delete m_pThrUpdater;
                    m_pThrUpdater = NULL;
                }
            }
        );
        thr.detach();
    }

    return started;
}

void SkySolverConnector::clearMountPoints()
{
    if (isSolveRunning())
        stopSolve();

    std::unique_lock lock(m_mountPtsMutex);
    m_mountPtPending = false;
    m_mountPts.clear();
    m_mountPtsDTOUpdated = true;
}

bool SkySolverConnector::isMountPointPending()
{
    return m_mountPtPending;
}

std::vector<SkySolverConnector::PlatesolveDTO>& SkySolverConnector::getMountPointsDTO()
{
    if (m_mountPtsDTOUpdated) {
        m_mountPtsDTOUpdated = false;
        m_mountPtsDTO.clear();
        {
            std::shared_lock lock(m_mountPtsMutex);
            for (size_t i = 0; i < m_mountPts.size(); i++) {
                PlatesolveResult& pt = m_mountPts[i];
                PlatesolveDTO dto;
                dto.ra = pt.ra;
                dto.dec = pt.dec;
                dto.pixel_scale = pt.pixel_scale;
                dto.field_size_x = pt.field_size.x;
                dto.field_size_y = pt.field_size.y;
                dto.rotation_angle = pt.rotation_angle_E_of_N;
                dto.parity = pt.parity;
                m_mountPtsDTO.push_back(dto);
            }
        }
    }
    
    return m_mountPtsDTO;
}

void SkySolverConnector::resetLastPlatesolveResult()
{
    std::unique_lock lock(m_lastPlateSolveResultMutex);
    auto& r = m_lastPlateSolveResult;
    r.ra = r.dec = r.pixel_scale = 0.0;
    r.field_size.x = r.field_size.y = 0.0;
    r.rotation_angle_E_of_N = 0.0;
    r.parity = false;
    r.success = r.valid = false;
    r.error.clear();
}

const SkySolverConnector::PlatesolveResult& SkySolverConnector::getLastPlatesolveResult()
{
    static thread_local PlatesolveResult res;
    {
        std::shared_lock lock(m_lastPlateSolveResultMutex);
        res = m_lastPlateSolveResult;
    }
    return res;
}

void SkySolverConnector::loopUpdatePlatesolveResult()
{
    printf("[D] SkySolverConnector::loopUpdatePlatesolveResult: START.\n");
    while (m_isSolveRunning) {

        std::string status;
        if (!ping() || !getStatus(status)) {
            printf("[E] SkySolverConnector::loopUpdatePlatesolveResult: Server unreachable.\n");
            {
                std::unique_lock lock(m_lastPlateSolveResultMutex);
                m_isSolveRunning = false;
                m_isSolveFinished = false;
            }
            resetLastPlatesolveResult();
            break;
        }

        /*****/if ("idle" == status)
        {
            printf("[E] SkySolverConnector::loopUpdatePlatesolveResult: Server in IDLE state.\n");
            {
                std::unique_lock lock(m_lastPlateSolveResultMutex);
                m_isSolveRunning = false;
                m_isSolveFinished = false;
            }
            resetLastPlatesolveResult();
            break;
        }
        /*****/else if ("initializing" == status)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        /*****/else if ("running" == status)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        /*****/else if ("cancelled" == status)
        {
            resetLastPlatesolveResult();
            break;
        }
        /*****/else if ("failed" == status)
        {

            std::string reason;
            if (!getFailReason(reason)) {
                printf("[E] SkySolverConnector::loopUpdatePlatesolveResult: Server unreachable.\n");
                {
                    std::unique_lock lock(m_lastPlateSolveResultMutex);
                    m_isSolveRunning = false;
                    m_isSolveFinished = false;
                }
                resetLastPlatesolveResult();
                break;
            }

            std::unique_lock lock(m_lastPlateSolveResultMutex);
            auto& r = m_lastPlateSolveResult;
            r.success = false;
            r.valid = true;
            r.error = reason;

            m_isSolveRunning = false;
            m_isSolveFinished = true;
            break;
        }
        /*****/else if ("completed" == status)
        {
            PlatesolveResult result;
            if (!getCompletionResult(result)) {
                printf("[E] SkySolverConnector::loopUpdatePlatesolveResult: Server unreachable.\n");
                {
                    std::unique_lock lock(m_lastPlateSolveResultMutex);
                    m_isSolveRunning = false;
                    m_isSolveFinished = false;
                }
                resetLastPlatesolveResult();
                break;
            }

            std::unique_lock lock(m_lastPlateSolveResultMutex);
            m_isSolveRunning = false;
            m_isSolveFinished = true;

            if (!result.valid && !result.error.empty()) {
                printf("[E] SkySolverConnector::loopUpdatePlatesolveResult: %s.\n", result.error.c_str());
                resetLastPlatesolveResult();
                break;
            }

            if (result.valid && result.success)
                m_lastPlateSolveResult = result;

            break;
        }
        /*****/else
        {
            printf("[E] SkySolverConnector::loopUpdatePlatesolveResult: Unknown Status (%s).\n", status.c_str());
            break;
        }

    }
    printf("[D] SkySolverConnector::loopUpdatePlatesolveResult: END.\n");
    {
        std::unique_lock lock(m_lastPlateSolveResultMutex);
        m_isSolveRunning = false;
        m_isSolveFinished = true;
    }
}

bool SkySolverConnector::getStatus(
    std::string& status
) {
    if (!m_isConnected)
        return false;

    const char* msg = "STATUS";
    int result = send(m_sock, msg, (int)strlen(msg), 0);
    if (SOCKET_ERROR == result) {
        disconnect();
        return false;
    }

    char buffer[256] = { 0 };
    int bytes = recv(m_sock, buffer, sizeof(buffer), 0);
    if (bytes <= 0) {
        disconnect();
        return false;
    }

    std::string response(buffer, bytes);
    size_t pos = response.find_first_of(':');
    status = response.substr(pos + 1);

    return true;
}

bool SkySolverConnector::getFailReason(
    std::string& reason
) {
    if (!m_isConnected)
        return false;

    const char* msg = "RESULT";
    int result = send(m_sock, msg, (int)strlen(msg), 0);
    if (SOCKET_ERROR == result) {
        disconnect();
        return false;
    }

    char buffer[1024] = { 0 };
    int bytes = recv(m_sock, buffer, sizeof(buffer), 0);
    if (bytes <= 0) {
        disconnect();
        return false;
    }

    std::string response(buffer, bytes);
    if (response.find("RESULT:failed") == std::string::npos)
        throw std::exception("SkySolverConnector::getFailReason INCORRECT USAGE.");

    auto arg = splitArgs(response)[0];
    reason = arg.substr(arg.find_first_of(':') + 1);

    return true;
}

bool SkySolverConnector::getCompletionResult(
    PlatesolveResult& r
) {
    if (!m_isConnected)
        return false;

    const char* msg = "RESULT";
    int result = send(m_sock, msg, (int)strlen(msg), 0);
    if (SOCKET_ERROR == result) {
        disconnect();
        return false;
    }

    char buffer[4096] = { 0 };
    int bytes = recv(m_sock, buffer, sizeof(buffer), 0);
    if (bytes <= 0) {
        disconnect();
        return false;
    }

    std::string response(buffer, bytes);
    if (response.find("RESULT:completed") == std::string::npos)
        throw std::exception("SkySolverConnector::getCompletionResult INCORRECT USAGE.");

    auto args = splitArgs(response);
    for (const auto& arg : args) {
        size_t pos = arg.find_first_of(':');
        std::string key = arg.substr(0, pos);
        std::string val = arg.substr(pos + 1);

        /*****/if ("RA" == key)
        {
            if (!myStoD(val, r.ra)) {
                printf("[E] SkySolverConnector::getCompletionResult: bad RA.\n");
                r.error = "bad RA";
                r.success = r.valid = false;
                break;
            }
        }
        /*****/else if ("DEC" == key)
        {
            if (!myStoD(val, r.dec)) {
                printf("[E] SkySolverConnector::getCompletionResult: bad DEC.\n");
                r.error = "bad DEC";
                r.success = r.valid = false;
                break;
            }
        }
        /*****/else if ("RA_HMS" == key)
        {
            /*unused*/
        }
        /*****/else if ("DEC_DMS" == key)
        {
            /*unused*/
        }
        /*****/else if ("PIXEL_SCALE" == key)
        {
            if (!myStoD(val, r.pixel_scale)) {
                printf("[E] SkySolverConnector::getCompletionResult: bad PIXEL_SCALE.\n");
                r.error = "bad PIXEL_SCALE";
                r.success = r.valid = false;
                break;
            }
        }
        /*****/else if ("FIELD_SIZE" == key)
        {
            size_t pos1 = val.find_first_of('x');
            size_t pos2 = val.find_last_of(' ');

            //123.45 x 12.34 units
            //       ^      ^
            //       |      |
            //      pos1    |
            //             pos2

            std::string field_x = val.substr(0, pos1 - 1);
            std::string field_y = val.substr(pos1 + 2, pos2 - pos1 - 2);
            std::string scale_unit = val.substr(pos2 + 1);

            double x, y;
            if (!myStoD(field_x, x) || !myStoD(field_y, y)) {
                printf("[E] SkySolverConnector::getCompletionResult: bad FIELD_SIZE.\n");
                r.error = "bad FIELD_SIZE";
                r.success = r.valid = false;
                break;
            }

            if (scale_unit.find("deg") != std::string::npos) {
                x *= 3600; y *= 3600;
            }
            else if (scale_unit.find("arcmin") != std::string::npos) {
                x *= 60;   y *= 60;
            }
            else if (scale_unit.find("arcsec") == std::string::npos) {
                printf("[E] SkySolverConnector::getCompletionResult: bad FIELD_SIZE scale_unit.\n");
                r.error = "bad FIELD_SIZE scale_unit";
                r.success = r.valid = false;
                break;
            }

            r.field_size.x = x;
            r.field_size.y = y;
        }
        /*****/else if ("ROTATION" == key)
        {
            size_t pos = val.find_first_of(' ');
            double rotation_angle;
            if (!myStoD(val.substr(0, pos), rotation_angle)) {
                printf("[E] SkySolverConnector::getCompletionResult: bad ROTATION.\n");
                r.error = "bad ROTATION";
                r.success = r.valid = false;
                break;
            }
            if (val.find("E of N") == std::string::npos) {
                printf("[E] SkySolverConnector::getCompletionResult: bad ROTATION direction.\n");
                r.error = "bad ROTATION direction";
                r.success = r.valid = false;
                break;
            }
            r.rotation_angle_E_of_N = rotation_angle;
        }
        /*****/else if ("PARITY" == key)
        {
            if (val.find("pos") != std::string::npos)
                r.parity = true;
            else if (val.find("neg") != std::string::npos)
                r.parity = false;
            else {
                printf("[E] SkySolverConnector::getCompletionResult: bad PARITY.\n");
                r.error = "bad PARITY";
                r.success = r.valid = false;
                break;
            }
        }
    }

    return true;
}

std::vector<std::string> SkySolverConnector::splitArgs(
    std::string response,
    char delimiter
) {
    std::vector<std::string> result{};

    // отрезаем первый элемент, т.к. он не является аргументом
    size_t pos = response.find_first_of(delimiter);
    if (pos != std::string::npos)
        response.erase(response.begin(), response.begin() + pos + 1);
    else
        response = "";

    pos = response.find_first_of(delimiter);
    while (pos != std::string::npos) {
        std::string arg(response.begin(), response.begin() + pos);
        result.emplace_back(arg);
        response.erase(response.begin(), response.begin() + pos + 1);
        pos = response.find_first_of(delimiter);
    }
    result.emplace_back(response);

    return result;
}

bool SkySolverConnector::myStoD(
    const std::string& str,
    double& d
) {
    try {
        d = std::stod(str);
    }
    catch (std::exception&) {
        return false;
    }
    return true;
}
