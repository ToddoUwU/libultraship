#include "ship/controller/physicaldevice/ConnectedPhysicalDeviceManager.h"
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <spdlog/spdlog.h>

namespace Ship {
ConnectedPhysicalDeviceManager::ConnectedPhysicalDeviceManager() {
}

ConnectedPhysicalDeviceManager::~ConnectedPhysicalDeviceManager() {
}

std::unordered_map<int32_t, SDL_GameController*>
ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadsForPort(uint8_t portIndex) {
    std::unordered_map<int32_t, SDL_GameController*> result;

    for (const auto& [instanceId, gamepad] : mConnectedSDLGamepads) {
        if (!PortIsIgnoringInstanceId(portIndex, instanceId)) {
            result[instanceId] = gamepad;
        }
    }

    return result;
}

std::unordered_map<int32_t, std::string> ConnectedPhysicalDeviceManager::GetConnectedSDLGamepadNames() {
    return mConnectedSDLGamepadNames;
}

std::unordered_set<int32_t> ConnectedPhysicalDeviceManager::GetIgnoredInstanceIdsForPort(uint8_t portIndex) {
    return mIgnoredInstanceIds[portIndex];
}

bool ConnectedPhysicalDeviceManager::PortIsIgnoringInstanceId(uint8_t portIndex, int32_t instanceId) {
    return GetIgnoredInstanceIdsForPort(portIndex).contains(instanceId);
}

void ConnectedPhysicalDeviceManager::IgnoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {
    mIgnoredInstanceIds[portIndex].insert(instanceId);
}

void ConnectedPhysicalDeviceManager::UnignoreInstanceIdForPort(uint8_t portIndex, int32_t instanceId) {
    mIgnoredInstanceIds[portIndex].erase(instanceId);
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceConnect(int32_t sdlDeviceIndex) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::HandlePhysicalDeviceDisconnect(int32_t sdlJoystickInstanceId) {
    RefreshConnectedSDLGamepads();
}

void ConnectedPhysicalDeviceManager::RefreshConnectedSDLGamepads() {
    // Couch seating (PersonalUpdates): remember the pads that were already connected, so the ports they were
    // given survive the refresh that every controller add/remove event triggers.
    auto previouslyConnected = std::move(mConnectedSDLGamepads);
    std::vector<int32_t> newInstanceIds;
    mConnectedSDLGamepads.clear();
    mConnectedSDLGamepadNames.clear();
    static SDL_JoystickGUID sZeroGuid;

    for (int32_t i = 0; i < SDL_NumJoysticks(); i++) {

        SDL_JoystickGUID deviceGUID = SDL_JoystickGetDeviceGUID(i);
        if (SDL_memcmp(&deviceGUID, &sZeroGuid, sizeof(deviceGUID)) == 0) {
            SPDLOG_WARN(
                "Calling SDL JoystickGetDeviceGUID with index ({:d}) returned zero GUID. This is likely due to an "
                "invalid index. Refer to https://wiki.libsdl.org/SDL2/SDL_JoystickGetDeviceGUID for more information.",
                i);
            continue;
        }

        char deviceGuidCStr[33] = "";
        SDL_JoystickGetGUIDString(deviceGUID, deviceGuidCStr, sizeof(deviceGuidCStr));

        if (!SDL_IsGameController(i)) {
            SPDLOG_WARN("SDL Joystick (GUID: {}) not recognized as gamepad."
                        "This is likely due to a missing mapping string in gamecontrollerdb.txt."
                        "Refer to https://github.com/mdqinc/SDL_GameControllerDB for more information.",
                        deviceGuidCStr);
            continue;
        }

        auto gamepad = SDL_GameControllerOpen(i);
        if (gamepad == nullptr) {
            SPDLOG_ERROR("SDL GameControllerOpen error (GUID: {}): {}", deviceGuidCStr, SDL_GetError());
            continue;
        }

        auto instanceId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad));
        if (instanceId < 0) {
            SPDLOG_ERROR("SDL JoystickInstanceID error (GUID: {}): {}", deviceGuidCStr, SDL_GetError());
            continue;
        }

        std::string gamepadName;
        auto name = SDL_GameControllerName(gamepad);
        if (name == nullptr) {
            gamepadName = deviceGuidCStr;
            SPDLOG_WARN("SDL_GameControllerName returned null. Setting name to GUID \"{}\" instead.", gamepadName);
        } else {
            gamepadName = name;
        }

        mConnectedSDLGamepads[instanceId] = gamepad;
        mConnectedSDLGamepadNames[instanceId] = gamepadName;

        if (!previouslyConnected.contains(instanceId)) {
            newInstanceIds.push_back(instanceId);
        }
    }

    const char* autoSeat = std::getenv("LUS_AUTO_SEAT");
    if (autoSeat != nullptr && std::string(autoSeat) == "0") {
        // Stock behaviour: every pad drives port 1 only.
        for (const auto& [instanceId, gamepad] : mConnectedSDLGamepads) {
            for (uint8_t port = 1; port < 4; port++) {
                mIgnoredInstanceIds[port].insert(instanceId);
            }
        }
        return;
    }

    // Seat new pads in plug order (SDL instance ids grow as devices are opened), each on the
    // lowest port that has no connected pad yet. Hide each new pad from every port first, so
    // the occupancy scan does not count the pad being placed.
    std::sort(newInstanceIds.begin(), newInstanceIds.end());
    for (int32_t newId : newInstanceIds) {
        for (uint8_t port = 0; port < 4; port++) {
            mIgnoredInstanceIds[port].insert(newId);
        }
    }
    for (int32_t newId : newInstanceIds) {
        uint8_t freePort = UINT8_MAX;
        for (uint8_t port = 0; port < 4 && freePort == UINT8_MAX; port++) {
            bool occupied = false;
            for (const auto& [instanceId, gamepad] : mConnectedSDLGamepads) {
                if (!mIgnoredInstanceIds[port].contains(instanceId)) {
                    occupied = true;
                    break;
                }
            }
            if (!occupied) {
                freePort = port;
            }
        }
        if (freePort == UINT8_MAX) {
            SPDLOG_INFO("Couch seating: all 4 ports have a gamepad; '{}' (instance {}) is not seated",
                        mConnectedSDLGamepadNames[newId], newId);
            continue;
        }
        mIgnoredInstanceIds[freePort].erase(newId);
        SPDLOG_INFO("Couch seating: gamepad '{}' (instance {}) -> port {}", mConnectedSDLGamepadNames[newId], newId,
                    freePort + 1);
    }
}
} // namespace Ship
