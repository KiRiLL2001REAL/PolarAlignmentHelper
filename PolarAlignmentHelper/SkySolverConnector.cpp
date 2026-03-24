#include "SkySolverConnector.h"

#include <cstdio>
#include <algorithm>
#include <filesystem>
#include <exception>

SkySolverConnector::SkySolverConnector() :
    m_socketMutex(),
    m_sock(INVALID_SOCKET),
    m_isConnected(false),
    m_serverIp(""),
    m_serverPort(0),

    m_thr(),
    m_thrWork(false)
{
    InitWinsock();

    m_thrWork = true;
    m_thr = std::thread(&SkySolverConnector::loop, this);
}

SkySolverConnector::~SkySolverConnector()
{
    m_thrWork = false;
    if (m_thr.joinable())
        m_thr.join();

    disconnect();
    CleanupWinsock();
}

void SkySolverConnector::setConnectionAddress(
    const std::string& ip,
    int port
) {
    m_serverIp = ip;
    m_serverPort = port;
}

bool SkySolverConnector::isConnected() const
{
    return m_isConnected;
}

bool SkySolverConnector::getStatus(
    std::string& status
) {
    if (!m_isConnected)
        return false;

    const char* msg = "STATUS";
    static const int buffer_length = 1024;
    char buffer[buffer_length] = { 0 };
    int bytes = 0;
    if (!sendNrecv(msg, buffer, buffer_length, &bytes))
        return false;

    std::string response(buffer, bytes);
    size_t pos = response.find_first_of(':');
    status = response.substr(pos + 1);

    return true;
}

bool SkySolverConnector::getResult(
    double& ra,
    double& dec,
    double& pixel_scale,
    double& field_size_x,
    double& field_size_y,
    double& rotation_angle,
    bool& parity,
    bool& success,
    std::filesystem::path& imgPath,
    std::string& error
) {
    if (!m_isConnected)
        return false;

    const char* msg = "RESULT";
    static const int buffer_length = 4096;
    char buffer[buffer_length] = { 0 };
    int bytes = 0;
    if (!sendNrecv(msg, buffer, buffer_length, &bytes))
        return false;

    std::string response(buffer, bytes);

    auto args = splitArgs(response);
    for (const auto& arg : args) {
        size_t pos = arg.find_first_of(':');
        std::string key = arg.substr(0, pos);
        std::string val = arg.substr(pos + 1);

        /*****/if ("RA" == key)
        {
            if (!myStoD(val, ra)) {
                printf("[E] SkySolverConnector::getResult: bad RA (%s).\n", val.c_str());
                error = "bad RA";
                success = false;
                break;
            }
        }
        /*****/else if ("DEC" == key)
        {
            if (!myStoD(val, dec)) {
                printf("[E] SkySolverConnector::getResult: bad DEC (%s).\n", val.c_str());
                error = "bad DEC";
                success = false;
                break;
            }
        }
        /*****/else if ("RA_HMS" == key || "DEC_DMS" == key)
        {
            /*unused*/
        }
        /*****/else if ("PIXEL_SCALE" == key)
        {
            if (!myStoD(val, pixel_scale)) {
                printf("[E] SkySolverConnector::getResult: bad PIXEL_SCALE (%s).\n", val.c_str());
                error = "bad PIXEL_SCALE";
                success = false;
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

            if (!myStoD(field_x, field_size_x) || !myStoD(field_y, field_size_y)) {
                printf("[E] SkySolverConnector::getResult: bad FIELD_SIZE (%s).\n", val.c_str());
                error = "bad FIELD_SIZE";
                success = false;
                break;
            }

            if (scale_unit.find("deg") != std::string::npos) {
                field_size_x *= 3600; field_size_y *= 3600;
            }
            else if (scale_unit.find("arcmin") != std::string::npos) {
                field_size_x *= 60;   field_size_y *= 60;
            }
            else if (scale_unit.find("arcsec") == std::string::npos) {
                printf("[E] SkySolverConnector::getResult: bad FIELD_SIZE scale_unit (%s).\n", val.c_str());
                error = "bad FIELD_SIZE scale_unit";
                success = false;
                break;
            }
        }
        /*****/else if ("ROTATION" == key)
        {
            size_t pos = val.find_first_of(' ');
            if (!myStoD(val.substr(0, pos), rotation_angle)) {
                printf("[E] SkySolverConnector::getResult: bad ROTATION (%s).\n", val.c_str());
                error = "bad ROTATION";
                success = false;
                break;
            }
            if (val.find("E of N") == std::string::npos) {
                printf("[E] SkySolverConnector::getResult: bad ROTATION direction (%s).\n", val.c_str());
                error = "bad ROTATION direction";
                success = false;
                break;
            }
        }
        /*****/else if ("PARITY" == key)
        {
            if (val.find("pos") != std::string::npos)
                parity = true;
            else if (val.find("neg") != std::string::npos)
                parity = false;
            else {
                printf("[E] SkySolverConnector::getResult: bad PARITY (%s).\n", val.c_str());
                error = "bad PARITY";
                success = false;
                break;
            }
        }
        /*****/else if ("IMAGE_PATH" == key)
        {
            std::string imagePathStr = val;
            imagePathStr = imagePathStr.substr(6);                              // убираем "/mnt/"
            imagePathStr[0] = std::toupper(imagePathStr[0]);                    // поднимаем регистр буквы диска
            std::replace(imagePathStr.begin(), imagePathStr.end(), '/', '\\');  // заменяем unix-styled слэши на windows-styled

            imgPath = std::filesystem::path(imagePathStr);
        }
        /*****/else if ("ERROR" == key)
        {
            error = val;
            success = false;
        }
    }

    return true;
}

bool SkySolverConnector::solve(
    const std::filesystem::path& imagePath,
    size_t timeLimitSec
) {
    if (!m_isConnected)
        return false;

    // преобразование пути Windows -> Linux
    std::string image_path_linux;
    {
        namespace fs = std::filesystem;
        fs::path p_filename = fs::weakly_canonical(imagePath);
        image_path_linux = p_filename.u8string();
        std::replace(image_path_linux.begin(), image_path_linux.end(), '\\', '/');
        image_path_linux.erase(image_path_linux.begin() + 1);
        image_path_linux[0] = std::tolower(image_path_linux[0]);
        image_path_linux = "/mnt/" + image_path_linux;
    }

    std::string cmd = "SOLVE:" + image_path_linux;
    if (timeLimitSec > 0)
        cmd += "|LIMIT:" + std::to_string(timeLimitSec);

    const char* msg = cmd.c_str();
    static const int buffer_length = 1024;
    char buffer[buffer_length] = { 0 };
    int bytes = 0;
    if (!sendNrecv(msg, buffer, buffer_length, &bytes))
        return false;

    std::string response(buffer, bytes);
    return response.find("OK:solve_started") != std::string::npos;
}

bool SkySolverConnector::cancel()
{
    if (!m_isConnected)
        return false;

    const char* msg = "CANCEL";
    static const int buffer_length = 1024;
    char buffer[buffer_length] = { 0 };
    int bytes = 0;
    if (!sendNrecv(msg, buffer, buffer_length, &bytes))
        return false;

    std::string response(buffer, bytes);
    return response.find("OK:") != std::string::npos;
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

bool SkySolverConnector::connect()
{
    if (m_isConnected)
        disconnect();

    std::scoped_lock(m_socketMutex);

    m_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (INVALID_SOCKET == m_sock) {
        printf("[E] SkySolverConnector::connect: Socket initialization error (code: %d).\n", WSAGetLastError());
        return false;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(m_serverPort);
    serverAddr.sin_addr.s_addr = inet_addr(m_serverIp.c_str());

    int result = ::connect(m_sock, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (SOCKET_ERROR == result) {
        printf("[E] SkySolverConnector::connect: Connection failed (code: %d).\n", WSAGetLastError());
        disconnect();
        return false;
    }

    m_isConnected = true;
    printf("[i] SkySolverConnector::connect: Connection established (%s:%d).\n", m_serverIp.c_str(), m_serverPort);

    return true;
}

void SkySolverConnector::disconnect()
{
    if (m_isConnected) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
        m_isConnected = false;
    }
}

bool SkySolverConnector::ping()
{
    if (!m_isConnected)
        return false;

    const char* msg = "PING";
    static const int buffer_length = 1024;
    char buffer[buffer_length] = { 0 };
    int bytes = 0;
    if (!sendNrecv(msg, buffer, buffer_length, &bytes))
        return false;

    std::string response = std::string(buffer, bytes);
    if ("PONG" != response)
        printf("[W] SkySolverConnector::ping: Strange anwser (%s), but server alive.\n", response.c_str());

    return true;
}

bool SkySolverConnector::sendNrecv(
    const char* msg,
    char* retBuffer,
    int retBufferLength,
    int* pRetBytes
) {
    std::scoped_lock lock(m_socketMutex);
    int result = send(m_sock, msg, (int)strlen(msg), 0);
    if (SOCKET_ERROR == result) {
        disconnect();
        return false;
    }
    *pRetBytes = recv(m_sock, retBuffer, retBufferLength, 0);
    if (*pRetBytes <= 0) {
        disconnect();
        return false;
    }
    return true;
}

void SkySolverConnector::loop()
{
    static const auto SERVER_CHECKING_INTERVAL = std::chrono::nanoseconds(2'500'000'000);  // 2500ms
    std::chrono::high_resolution_clock::time_point last_checkout;

    printf("[D] SkySolverConnector::loopConnectAndPing: START.\n");
    while (m_thrWork) {
        last_checkout = std::chrono::high_resolution_clock::now();

        if (isConnected()) ping();
        else               connect();

        auto elapsed = std::chrono::high_resolution_clock::now() - last_checkout;
        if (elapsed >= SERVER_CHECKING_INTERVAL) continue;
        std::this_thread::sleep_for(std::chrono::nanoseconds(SERVER_CHECKING_INTERVAL - elapsed));
    }
    printf("[D] SkySolverConnector::loopConnectAndPing: END.\n");
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
