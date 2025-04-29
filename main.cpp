#include <iostream>
#include <cstring>
#include <cmath>
#include <thread>
#include <chrono>
#include <SDL2/SDL.h>
#include <SDL2/SDL_joystick.h>
#include <SDL2/SDL_haptic.h>

#include <winsock2.h>
#include "vjoyinterface.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "vJoyInterface.lib")

#define INSIM_PORT 29999
#define MAX_PACKET_SIZE 512

const UINT vJoyDeviceID = 1;

#pragma pack(push, 1)
struct OutSimPacket {
    uint32_t time;
    uint32_t car_id;
    float pos_x, pos_y, pos_z;
    float heading, pitch, roll;
    float vel_x, vel_y, vel_z;
    float ang_vel_x, ang_vel_y, ang_vel_z;
    float wheel_speed[4];
};
#pragma pack(pop)

template<typename T>
T clamp(T value, T minVal, T maxVal) {
    if (value < minVal) return minVal;
    if (value > maxVal) return maxVal;
    return value;
}

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

SDL_Haptic* haptic = nullptr;
SDL_HapticEffect constantEffect;
int effect_id = -1;

void initialize_haptic(SDL_Joystick* joystick) {
    if (!joystick) return;

    haptic = SDL_HapticOpenFromJoystick(joystick);
    if (!haptic) {
        std::cerr << "Haptic init failed: " << SDL_GetError() << std::endl;
        return;
    }

    if (!SDL_HapticQuery(haptic) & SDL_HAPTIC_CONSTANT) {
        std::cerr << "Haptic constant effect not supported" << std::endl;
        SDL_HapticClose(haptic);
        haptic = nullptr;
        return;
    }

    SDL_memset(&constantEffect, 0, sizeof(SDL_HapticEffect));
    constantEffect.type = SDL_HAPTIC_CONSTANT;
    constantEffect.constant.direction.type = SDL_HAPTIC_CARTESIAN;
    constantEffect.constant.direction.dir[0] = 1;
    constantEffect.constant.length = SDL_HAPTIC_INFINITY;
    constantEffect.constant.level = 0;
    constantEffect.constant.attack_length = 100;
    constantEffect.constant.fade_length = 100;

    effect_id = SDL_HapticNewEffect(haptic, &constantEffect);
    if (effect_id < 0) {
        std::cerr << "Failed to create haptic effect: " << SDL_GetError() << std::endl;
        SDL_HapticClose(haptic);
        haptic = nullptr;
        return;
    }

    SDL_HapticRunEffect(haptic, effect_id, SDL_HAPTIC_INFINITY);
}

void apply_ffb_force(int16_t force) {
    if (!haptic || effect_id < 0) return;
    constantEffect.constant.level = force;
    SDL_HapticUpdateEffect(haptic, effect_id, &constantEffect);
}

int main() {
    if (SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC) != 0) {
        std::cerr << "SDL Init error: " << SDL_GetError() << std::endl;
        return 1;
    }

    SDL_Joystick* realWheel = nullptr;
    if (SDL_NumJoysticks() > 0) {
        realWheel = SDL_JoystickOpen(0);
        if (realWheel) {
            std::cout << "Opened steering wheel: " << SDL_JoystickName(realWheel) << std::endl;
            initialize_haptic(realWheel);
        } else {
            std::cerr << "Failed to open joystick: " << SDL_GetError() << std::endl;
        }
    } else {
        std::cerr << "No joystick detected!" << std::endl;
    }

    if (!initialize_vjoy()) {
        return 1;
    }

    SOCKET udpSocket = initialize_udp_socket();
    char buffer[MAX_PACKET_SIZE];

    std::cout << "Running telemetry receiver + input proxy..." << std::endl;

    float prevSteer = 0.0f;

    while (true) {
        sockaddr_in clientAddr;
        int clientAddrSize = sizeof(clientAddr);
        int recvLen = recvfrom(udpSocket, buffer, MAX_PACKET_SIZE, 0, (sockaddr*)&clientAddr, &clientAddrSize);
        if (recvLen >= sizeof(OutSimPacket)) {
            OutSimPacket packet;
            memcpy(&packet, buffer, sizeof(OutSimPacket));

            float forwardVel = packet.vel_y;
            float lateralVel = packet.vel_x;
            float speed = std::sqrt(forwardVel * forwardVel + lateralVel * lateralVel);
            float slipAngle = std::atan2(lateralVel, forwardVel);

            SDL_JoystickUpdate();
            int16_t axisValue = SDL_JoystickGetAxis(realWheel, 0);
            float steerNorm = axisValue / 32767.0f;

            float steerRate = (steerNorm - prevSteer) / 0.005f;
            float K_spring = 5.0f;
            float K_damp = 0.8f;
            float K_slip = 15.0f;

            float ff_force = -K_spring * steerNorm - K_damp * steerRate - K_slip * slipAngle;
            prevSteer = steerNorm;

            ff_force = clamp(ff_force, -1.0f, 1.0f);
            int16_t ffbValue = static_cast<int16_t>(ff_force * 32767.0f);

            std::cout << "Speed: " << speed << " m/s, Slip: " << slipAngle
                      << " rad, FFB: " << ffbValue << std::endl;

            LONG vJoySteer = static_cast<LONG>((steerNorm + 1.0f) * 0.5f * 32767);
            SetAxis(vJoySteer, vJoyDeviceID, HID_USAGE_X);

            apply_ffb_force(ffbValue);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (haptic) SDL_HapticDestroyEffect(haptic, effect_id);
    if (haptic) SDL_HapticClose(haptic);
    if (realWheel) SDL_JoystickClose(realWheel);
    closesocket(udpSocket);
    WSACleanup();
    SDL_Quit();
    return 0;
}
