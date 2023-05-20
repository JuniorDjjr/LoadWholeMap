#include "plugin.h"
#include "..\injector\assembly.hpp"
#include "CStreaming.h"
#include "IniReader/IniReader.h"
#include "CIplStore.h"
#include "extensions/ScriptCommands.h"
#include "CMessages.h"
#include "CTimer.h"
#include "CPopCycle.h"
#include "CAnimManager.h"
#include <vector>

using namespace plugin;
using namespace std;
using namespace injector;

const int MAX_MB = 2000;
const int MULT_MB_TO_BYTE = 1048576;
constexpr unsigned int MAX_BYTE_LIMIT = (MAX_MB * MULT_MB_TO_BYTE);

// Variables estáticas
static unsigned int streamMemoryForced = 0;
static int totalBinaryIPLconfig = 0;
static int totalBinaryIPLloaded = 0;
static int loadCheck = 0;
static float removeUnusedWhenPercent = 0.0f;
static int lastTimeRemoveUnused = 0;
static int gameStartedAfterLoad = 0;
static int removeUnusedIntervalMs = 0;
static fstream lg;
static CIniReader ini("ImprovedStreaming.ini");
static bool loadBinaryIPLs = ini.ReadInteger("Settings", "LoadBinaryIPLs", 0) == 1;
static bool preLoadLODs = ini.ReadInteger("Settings", "PreLoadLODs", 0) == 1;
static bool preLoadAnims = ini.ReadInteger("Settings", "PreLoadAnims", 0) == 1;
static int logMode = ini.ReadInteger("Settings", "LogMode", -1);
static std::vector<void*> lods; // CEntity*

const int INCREASE_STREAMING_MEMORY_LIMIT = 256;

class LoadWholeMap {
public:
    LoadWholeMap() {
        if (logMode >= 0) {
            lg.open("ImprovedStreaming.log", fstream::out | fstream::trunc);
            lg << "v1.0" << endl;
        }

        Events::initRwEvent += [] {
            if (GetModuleHandleA("LoadWholeMap.SA.asi")) {
                MessageBoxA(0, "'LoadWholeMap.SA.asi' is now 'ImprovedStreaming.SA.asi'. Delete the old mod.", "ImprovedStreaming.SA.asi", 0);
            }

            if (preLoadLODs) {
                injector::MakeInline<0x5B5295, 0x5B5295 + 8>([](injector::reg_pack& regs) {
                    regs.ecx = *(uint16_t*)(regs.eax + 0x22);
                    regs.edx = *(uint16_t*)(regs.esi + 0x22);
                    lods.push_back((void*)regs.eax);
                });
            }
        };

        Events::initScriptsEvent.after += [] {
            loadCheck = 1;
        };

        Events::processScriptsEvent.after += [] {
            if (loadCheck < 3) {
                loadCheck++;
                return;
            }

            if (streamMemoryForced > 0 && CStreaming::ms_memoryAvailable < streamMemoryForced) {
                CStreaming::ms_memoryAvailable = streamMemoryForced;
            }

            if (loadCheck == 3) {
                if (!(GetKeyState(0x10) & 0x8000)) {
                    streamMemoryForced = ini.ReadInteger("Settings", "StreamMemoryForced", 0);
                    if (streamMemoryForced > 0) {
                        if (streamMemoryForced > MAX_MB) {
                            streamMemoryForced = MAX_BYTE_LIMIT;
                        } else {
                            streamMemoryForced *= MULT_MB_TO_BYTE;
                        }
                        CStreaming::ms_memoryAvailable = streamMemoryForced;
                    }

                    removeUnusedWhenPercent = ini.ReadFloat("Settings", "RemoveUnusedWhenPercent", 0.0f);
                    removeUnusedIntervalMs = ini.ReadInteger("Settings", "RemoveUnusedInterval", 60);

                    CTimer::Stop();

                    if (preLoadLODs && !lods.empty()) {
                        std::vector<uint16_t> lods_id(lods.size());
                        std::transform(lods.begin(), lods.end(), lods_id.begin(), [](void* entity) {
                            return *(uint16_t*)((uintptr_t)(entity)+0x22);
                        });

                        std::for_each(lods_id.begin(), std::unique(lods_id.begin(), lods_id.end()), [](uint16_t id) {
                            CStreaming::RequestModel(id, eStreamingFlags::MISSION_REQUIRED);
                        });
                        CStreaming::LoadAllRequestedModels(false);

                        std::for_each(lods.begin(), lods.end(), [](void* entity) {
                            auto rwObject = *(void**)((uintptr_t)(entity)+0x18);
                            if (rwObject == nullptr) {
                                CallMethod<0x533D30>(entity);
                            }
                        });
                    }

                    if (preLoadAnims) {
                        int animBlocksIdStart = injector::ReadMemory<int>(0x48C36B + 2, true);
                        for (int id = 1; id < CAnimManager::ms_numAnimBlocks; ++id) {
                            if (logMode >= 1) {
                                lg << "Start loading anim " << id << endl;
                            }
                            CStreaming::RequestModel(id + animBlocksIdStart, eStreamingFlags::MISSION_REQUIRED);
                            CAnimManager::AddAnimBlockRef(id);
                        }
                        CStreaming::LoadAllRequestedModels(false);
                        if (logMode >= 1) {
                            lg << "Finished loading anims." << endl;
                        }
                    }

                    int i = 0;
                    while (true) {
                        i++;

                        string range = "Range" + to_string(i);

                        int startId = ini.ReadInteger(range, "Start", -1);
                        int endId = ini.ReadInteger(range, "End", -1);

                        if (startId <= 0 && endId <= 0) break;

                        if (ini.ReadInteger(range, "Enabled", 0) != 1) continue;

                        if (logMode >= 0) {
                            lg << "Start loading ID Range: " << i << endl;
                            lg.flush();
                        }

                        if (endId >= startId) {
                            for (int model = startId; model <= endId; model++) {
                                if (GetKeyState(0x10) & 0x8000) break;
                                if (CStreaming::ms_aInfoForModel[model].m_nCdSize != 0) {
                                    check_limit_to_load:
                                    if (CStreaming::ms_memoryUsed > (CStreaming::ms_memoryAvailable - 50000000)) {
                                        if (CStreaming::ms_memoryAvailable >= MAX_BYTE_LIMIT) {
                                            if (logMode >= 0) {
                                                lg << "ERROR: Not enough space" << endl;
                                            }
                                            CMessages::AddMessageJumpQ((char*)"~r~ERROR Load Whole Map: Not enough space. Try to disable some ranges, configure or use other settings.", 8000, false, false);
                                        } else {
                                            if (streamMemoryForced > 0) {
                                                if (IncreaseStreamingMemoryLimit(INCREASE_STREAMING_MEMORY_LIMIT)) {
                                                    streamMemoryForced = CStreaming::ms_memoryAvailable;
                                                    if (logMode >= 0) {
                                                        lg << "Streaming memory automatically increased to " << streamMemoryForced << endl;
                                                    }
                                                    goto check_limit_to_load;
                                                }
                                            } else {
                                                if (logMode >= 0) {
                                                    lg << "ERROR: Not enough space. Try to increase the streaming memory." << endl;
                                                }
                                                CMessages::AddMessageJumpQ((char*)"~r~ERROR Load Whole Map: Not enough space. Try to increase the streaming memory.", 8000, false, false);
                                            }
                                        }
                                        if (logMode >= 0) {
                                            lg.flush();
                                        }
                                        break;
                                    } else {
                                        if (logMode >= 1) {
                                            lg << "Loading " << model << " size " << CStreaming::ms_aInfoForModel[model].m_nCdSize << endl;
                                            lg.flush();
                                        }
                                        CStreaming::RequestModel(model, eStreamingFlags::MISSION_REQUIRED);
                                    }
                                }
                            }
                            CStreaming::LoadAllRequestedModels(false);
                        }
                        if (logMode >= 0) {
                            lg << "Finished loading ID Range: " << i << endl;
                            lg.flush();
                        }
                    }

                    if (CStreaming::ms_memoryUsed > (CStreaming::ms_memoryAvailable - 50000000)) {
                        if (IncreaseStreamingMemoryLimit(INCREASE_STREAMING_MEMORY_LIMIT)) {
                            streamMemoryForced = CStreaming::ms_memoryAvailable;
                            if (logMode >= 0) {
                                lg << "Streaming memory automatically increased to " << streamMemoryForced << " after loading (margin)." << endl;
                            }
                        }
                    }

                    if (logMode >= 0) {
                        lg.flush();
                    }
                    CTimer::Update();
                    gameStartedAfterLoad = CTimer::m_snTimeInMilliseconds;
                }

                loadCheck = 4;
            }

            if (loadCheck == 4 && removeUnusedWhenPercent > 0.0) {
                float memUsedPercent = (float)((float)CStreaming::ms_memoryUsed / (float)CStreaming::ms_memoryAvailable) * 100.0f;
                if (memUsedPercent >= removeUnusedWhenPercent && (CTimer::m_snTimeInMilliseconds - lastTimeRemoveUnused) > removeUnusedIntervalMs) {
                    CStreaming::RemoveLeastUsedModel(0);
                    lastTimeRemoveUnused = CTimer::m_snTimeInMilliseconds;
                }
            }
        };
    }

    static bool IncreaseStreamingMemoryLimit(unsigned int mb) {
        unsigned int increaseBytes = mb *= MULT_MB_TO_BYTE;
        unsigned int newLimit = CStreaming::ms_memoryAvailable + increaseBytes;
        if (newLimit <= 0) return false;
        if (newLimit >= MAX_BYTE_LIMIT) newLimit = MAX_BYTE_LIMIT;
        CStreaming::ms_memoryAvailable = newLimit;
        return true;
    }
};

LoadWholeMap loadWholeMap;
