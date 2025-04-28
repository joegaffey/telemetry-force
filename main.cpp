// main.cpp

#include <iostream>
#include <cstring>
#include <cmath>
#include <thread>
#include <chrono>
#include <SDL2/SDL.h>
#include <SDL2/SDL_joystick.h>

#include <winsock2.h>      // Windows sockets
#include "vjoyinterface.h" // vJoy SDK

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "vJoyInterface.lib")

// UDP settings
#define INSIM_PORT 29999
#define MAX_PACKET_SIZE 512

// vJoy settings
const UINT vJoyDeviceID = 1;

// OutSim telemetry packet
#pragma pack(push, 1)
struct OutSimPacket {
    uint32_t time;        // ms since start
    uint32_t car_id;      // car ID
    float pos_x;
    float pos_y;
    float pos_z;
    float heading;        // radians
    float pitch;
    float roll;
    float vel_x;
    float vel_y;
    float vel_z;
    float ang_vel_x;
    float ang_vel_y;
    float ang_vel_z;
    float wheel_speed[4]; // m/s
};
#pragma pack(pop)

// Clamp helper
template<typename T>
T clamp(T value, T minVal, T maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

// Initialize vJoy device
bool initialize_vjoy() {
    if (!vJoyEnabled()) {
        std::cerr << "vJoy driver not enabled" << std::endl;
        return false;
    }

    VjdStat status = GetVJDStatus(vJoyDeviceID);
    if (status == VJD_STAT_OWN || status == VJD_STAT_FREE) {
        if (!AcquireVJD(vJoyDeviceID)) {
            std::cerr << "Failed to acquire vJoy device" << std::endl;
            return false;
        }
    } else {
        std::cerr << "vJoy device busy or missing" << std::endl;
        return false;
    }

    std::cout << "vJoy device acquired successfully" << std::endl;
    return true;
}

// Initialize Winsock and UDP socket
SOCKET initialize_udp_socket() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed" << std::endl;
        exit(1);
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed" << std::endl;
        WSACleanup();
        exit(1);
    }

    sockaddr_in serverAddr = {};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(INSIM_PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Socket bind failed" << std::endl;
        closesocket(sock);
        WSACleanup();
        exit(1);
    }

    return sock;
}

int main(int argc, char* argv[]) {
    // Init SDL2 for Joystick
    if (SDL_Init(SDL_INIT_JOYSTICK) != 0) {
        std::cerr << "SDL Init error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Joystick* realWheel = nullptr;
    if (SDL_NumJoysticks() > 0) {
        realWheel = SDL_JoystickOpen(0);
        if (realWheel) {
            std::cout << "Opened steering wheel: " << SDL_JoystickName(realWheel) << std::endl;
        } else {
            std::cerr << "Failed to open joystick: " << SDL_GetError() << std::endl;
        }
    } else {
        std::cerr << "No joystick detected!" << std::endl;
    }

    // Init vJoy
    if (!initialize_vjoy()) {
        return 1;
    }

    // Init UDP socket
    SOCKET udpSocket = initialize_udp_socket();

    char buffer[MAX_PACKET_SIZE];

    std::cout << "Running telemetry receiver + input proxy..." << std::endl;

    // Main loop
    while (true) {
        // Receive OutSim data
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        int recvLen = recvfrom(udpSocket, buffer, MAX_PACKET_SIZE, 0, (sockaddr*)&clientAddr, &clientAddrSize);
        if (recvLen >= sizeof(OutSimPacket)) {
            OutSimPacket packet;
            memcpy(&packet, buffer, sizeof(OutSimPacket));

            float speed = std::sqrt(packet.vel_x * packet.vel_x + packet.vel_y * packet.vel_y);
            float heading = packet.heading;

            std::cout << "Speed: " << speed << " m/s, Heading: " << heading << " rad" << std::endl;
        }

        // Read real steering input
        if (realWheel) {
            SDL_JoystickUpdate(); // poll inputs
            int16_t axisValue = SDL_JoystickGetAxis(realWheel, 0); // Axis 0 = steering
            float normalizedSteer = axisValue / 32767.0f;
            normalizedSteer = clamp(normalizedSteer, -1.0f, 1.0f);

            // Map to vJoy range (0..32767)
            LONG vJoySteer = static_cast<LONG>((normalizedSteer + 1.0f) * 0.5f * 32767);

            // Send to vJoy
            if (!SetAxis(vJoySteer, vJoyDeviceID, HID_USAGE_X)) {
                std::cerr << "Failed to update vJoy steering axis" << std::endl;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5)); // avoid 100% CPU
    }

    // Cleanup
    if (realWheel) SDL_JoystickClose(realWheel);
    closesocket(udpSocket);
    WSACleanup();
    SDL_Quit();

    return 0;
}
