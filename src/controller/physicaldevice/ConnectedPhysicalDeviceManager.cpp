#include "ConnectedPhysicalDeviceManager.h"
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

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

    for (int32_t i = 0; i < SDL_NumJoysticks(); i++) {
        // skip if this SDL joystick isn't a Gamepad
        if (!SDL_IsGameController(i)) {
            continue;
        }

        auto gamepad = SDL_GameControllerOpen(i);
        auto instanceId = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(gamepad));
        auto name = SDL_GameControllerName(gamepad);

        mConnectedSDLGamepads[instanceId] = gamepad;
        mConnectedSDLGamepadNames[instanceId] = name;

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
