#include <iostream>
#include <fstream>
#include <unordered_map>
#include <vector>
#include <string>
#include <thread>
#include <chrono>

enum muxContactorType {
    NORMAL, SUPER, PRIORITY
};

struct muxContactor {
    int id;
    muxContactorType type;
};

std::vector<std::vector<muxContactor>> muxContactors;
std::unordered_map<std::string, bool> powerModuleStatus;
std::unordered_map<std::string, bool> gunFaultStatus;
std::ofstream logFile("mux_log.txt");

int indexFromGunId(const std::string& gunId) {
    return std::stoi(gunId.substr(1)); // G0 -> 0
}

std::string gunIdFromIndex(int index) {
    return "G" + std::to_string(index); // 0 -> G0
}

int twin(int index) {
    return (index % 2 == 0 ? index + 1 : index - 1);
}

void log(const std::string& msg) {
    std::cout << msg << std::endl;
    if (logFile.is_open()) logFile << msg << std::endl;
}

void simulateFault(const std::string& gunId) {
    gunFaultStatus[gunId] = true;
    log("FAULT simulated on " + gunId);
}

void primaryInternal(int i, const std::vector<bool>& gunStatus) {
    log("PrimaryInternal: P" + std::to_string(i) + ", Q" + std::to_string(i) + ", Q" + std::to_string(twin(i)));
    if (!gunStatus[twin(i)]) log("Additional: P" + std::to_string(twin(i)));
}

void siblingInternal(int i, const std::vector<bool>& gunStatus) {
    for (const auto& conn : muxContactors[i]) {
        if (conn.type == NORMAL) {
            log("SiblingInternal --> " + gunIdFromIndex(conn.id));
            primaryInternal(conn.id, gunStatus);
        }
    }
}

void external(int i, const std::vector<bool>& gunStatus) {
    for (const auto& conn : muxContactors[i]) {
        if (conn.type == SUPER) {
            log("External --> " + gunIdFromIndex(conn.id));
            primaryInternal(conn.id, gunStatus);
            siblingInternal(conn.id, gunStatus);
        }
    }
}

std::vector<std::string> allotPower(int index, int power, const std::vector<bool>& gunStatus, std::unordered_map<std::string, bool>& visitedModules) {
    const int defaultPower = 30;
    std::vector<std::string> output;
    std::string gunId = gunIdFromIndex(index);

    auto tryAssign = [&](const std::string& assignGunId, const std::string& moduleName) -> bool {
        if (!powerModuleStatus[moduleName] && !visitedModules[moduleName] && !gunFaultStatus[assignGunId]) {
            log("Assigning module: " + assignGunId + ":" + moduleName);
            output.push_back(assignGunId + ":" + moduleName);
            powerModuleStatus[moduleName] = true;
            visitedModules[moduleName] = true;
            return true;
        }
        return false;
    };

    // Primary internal allocation
    tryAssign(gunId, "P" + std::to_string(index));
    tryAssign(gunId, "Q" + std::to_string(index));
    tryAssign(gunId, "Q" + std::to_string(twin(index)));
    tryAssign(gunId, "P" + std::to_string(twin(index)));

    for (const auto& conn : muxContactors[index]) {
        if (conn.type == NORMAL && !gunStatus[conn.id] && !gunFaultStatus[gunIdFromIndex(conn.id)]) {
            tryAssign(gunIdFromIndex(conn.id), "P" + std::to_string(conn.id));
            tryAssign(gunIdFromIndex(conn.id), "Q" + std::to_string(conn.id));
            tryAssign(gunIdFromIndex(conn.id), "Q" + std::to_string(twin(conn.id)));
            tryAssign(gunIdFromIndex(conn.id), "P" + std::to_string(twin(conn.id)));
            output.push_back(gunId + ":" + gunIdFromIndex(conn.id));
        }
    }

    for (const auto& conn : muxContactors[index]) {
        if (conn.type == SUPER && !gunStatus[conn.id] && !gunFaultStatus[gunIdFromIndex(conn.id)]) {
            std::vector<bool> tempStatus = gunStatus;
            tempStatus[conn.id] = true;
            std::unordered_map<std::string, bool> tempVisited = visitedModules;
            std::vector<std::string> external = allotPower(conn.id, power, tempStatus, tempVisited);
            if (!external.empty()) {
                output.insert(output.end(), external.begin(), external.end());
                output.push_back(gunId + ":" + gunIdFromIndex(conn.id));
                for (const auto& assignment : external) {
                    size_t colon = assignment.find(":");
                    if (colon != std::string::npos) {
                        std::string module = assignment.substr(colon + 1);
                        visitedModules[module] = true;
                        powerModuleStatus[module] = true;
                    }
                }
            }
        }
    }

    return output;
}

std::vector<std::string> getSwitchActions(const std::vector<std::string>& allocations) {
    std::vector<std::string> actions;
    for (const auto& entry : allocations) {
        size_t colon = entry.find(":");
        if (colon != std::string::npos) {
            std::string source = entry.substr(0, colon);
            std::string target = entry.substr(colon + 1);
            actions.push_back("SWITCH ON: " + source + " --> " + target);
        }
    }
    return actions;
}

void buildMuxTopology() {
    muxContactors.resize(8);
    muxContactors[0] = {{2, NORMAL}, {3, NORMAL}, {7, SUPER}};
    muxContactors[1] = {{2, NORMAL}, {3, NORMAL}};
    muxContactors[2] = {{0, NORMAL}, {1, NORMAL}};
    muxContactors[3] = {{0, NORMAL}, {1, NORMAL}, {4, SUPER}};
    muxContactors[4] = {{6, NORMAL}, {7, NORMAL}, {3, SUPER}};
    muxContactors[5] = {{6, NORMAL}, {7, NORMAL}};
    muxContactors[6] = {{4, NORMAL}, {5, NORMAL}};
    muxContactors[7] = {{4, NORMAL}, {5, NORMAL}, {0, SUPER}};
}

int main() {
    for (int i = 0; i < 8; ++i) {
        powerModuleStatus["P" + std::to_string(i)] = false;
        powerModuleStatus["Q" + std::to_string(i)] = false;
        gunFaultStatus["G" + std::to_string(i)] = false;
    }

    buildMuxTopology();

    while (true) {
        log("\nSelect test:");
        log("1. Test G0");
        log("2. Test G1 (G0 OFF)");
        log("3. Test G5 + G6 ON");
        log("4. Simulate Fault on G3");
        log("5. Exit");
        int choice;
        std::cin >> choice;

        std::vector<bool> gunStatus(8, false);
        std::unordered_map<std::string, bool> visitedModules;
        for (const auto& status : powerModuleStatus) visitedModules[status.first] = false;

        switch (choice) {
            case 1: {
                std::string active = "G0";
                if (gunFaultStatus[active]) {
                    log(active + " is FAULTY. Skipping allocation.");
                    break;
                }
                gunStatus[0] = true;
                auto alloc = allotPower(0, 240, gunStatus, visitedModules);
                for (const auto& line : alloc) log("  " + line);
                for (const auto& act : getSwitchActions(alloc)) log(act);
                break;
            }
            case 2: {
                if (gunFaultStatus["G1"]) break;
                gunStatus[1] = true;
                auto alloc = allotPower(1, 240, gunStatus, visitedModules);
                for (const auto& line : alloc) log("  " + line);
                for (const auto& act : getSwitchActions(alloc)) log(act);
                break;
            }
            case 3: {
                gunStatus[5] = true;
                gunStatus[6] = true;
                auto alloc = allotPower(6, 240, gunStatus, visitedModules);
                for (const auto& line : alloc) log("  " + line);
                for (const auto& act : getSwitchActions(alloc)) log(act);
                break;
            }
            case 4:
                simulateFault("G3");
                break;
            case 5:
                logFile.close();
                return 0;
            default:
                log("Invalid choice");
        }
    }
}
