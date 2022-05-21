#include "library.h"
#include "shared/AML/amlmod.h"
#include "shared/ini/inireader.h"
#include "GTASA/CAnimManager.h"
#include "GTASA/CStreaming.h"
#include "GTASA/CTimer.h"
#include <fstream>
#include <vector>

//init class
CAnimManager AnimManager;
CStreaming Streaming;
CTimer Timer;

MYMOD(net.xmds.SAMImprovedStreaming, SAMImprovedStreaming, 1.1, XMDS & Junior_Djjr)
#define INIPath "/storage/emulated/0/Android/data/com.rockstargames.gtasa/configs/SAMImprovedStreaming.ini"
#define LOGPath "/storage/emulated/0/Android/data/com.rockstargames.gtasa/configs/SAMImprovedStreaming.log"

using namespace std;
const int MAX_MB = 2000;
const int MULT_MB_TO_BYTE = 1048576;
constexpr unsigned int MAX_BYTE_LIMIT = (MAX_MB * MULT_MB_TO_BYTE);

static unsigned int streamMemoryForced = 0;
static int totalBinaryIPLconfig = 0;
static int totalBinaryIPLloaded = 0;
static int loadCheck = 0;
static float removeUnusedWhenPercent = 0.0f;
static int lastTimeRemoveUnused = 0;
static int gameStartedAfterLoad = 0;
static int removeUnusedIntervalMs = 0;
static fstream lg;

static bool preLoadLODs;
static bool preLoadAnims;
static int logMode;
static std::vector<void*> lods;
static bool FLAFix = 0;

static bool IncreaseStreamingMemoryLimit(unsigned int mb)
{
	unsigned int increaseBytes = mb *= MULT_MB_TO_BYTE;
	unsigned int newLimit = *CStreaming::ms_memoryAvailable + increaseBytes;
	if (newLimit <= 0) return false;
	if (newLimit >= MAX_BYTE_LIMIT) newLimit = MAX_BYTE_LIMIT;
	*CStreaming::ms_memoryAvailable = newLimit;
	return true;
}

uintptr_t jmp_004691E2;

extern "C" void SaveEntitPointer(void* CEntity)
{
	lods.push_back(CEntity);
}

 //Hook injection Take the entity pointer and save it to the container
static __attribute__((target("thumb-mode"))) __attribute__((naked)) void Hook_004691D6()
{
	asm(
		".thumb\n"
		".hidden jmp_004691E2\n"
		"LDRSH.W R2, [R6,#0x26]\n"
		"LDRB.W R1, [R0,#0x38]\n"
		"MOV R12, R6\n"
		"PUSH {R0-R10}\n"
		"MOV R0, R12\n"
		"BL SaveEntitPointer\n"
		"POP {R0-R10}\n"
		"LDR.W R11, [R9,R2,LSL#2]\n"
		ASM_LOAD_4BYTE_UNSIGNED_VALUE_STORED_ON_SYMBOL(R12, jmp_004691E2)
		"BX R12\n" //return to Game addr
	);
}

bool (*initRwEvent)();

bool MyinitRwEvent()
{
	if (preLoadLODs) //injection
	{
		jmp_004691E2 = ASM_GET_THUMB_ADDRESS_FOR_JUMP(libs.pGame + 0x004691E2);
		CPatch::RedirectCodeEx(SET_THUMB, libs.pGame + 0x004691D6, (void*)Hook_004691D6);
	}
	return initRwEvent();
}

int (*initScriptsEvent)();

int MyinitScriptsEvent()
{
	bool ret = initScriptsEvent();
	//Compatible with FLA, fix ID limit, After the FLA takes effect, the ModeInfo pointer is obtained.
	CStreaming::ms_aInfoForModel = reinterpret_cast<CStreamingInfo*>(CPatch::GetPointer(libs.pGame + 0x00677DD8));
	loadCheck = 1;
	return ret;
}

void (*processScriptsEvent)();

void MyprocessScriptsEvent()
{
	processScriptsEvent();
	if (loadCheck < 3)
	{
		loadCheck++;
		return;
	}
	if (streamMemoryForced > 0)
	{
		if (*CStreaming::ms_memoryAvailable < streamMemoryForced) {
			CPatch::SetUint32((uintptr_t)CStreaming::ms_memoryAvailable, streamMemoryForced);
		}
	}

	if (loadCheck == 3)
	{
		
		streamMemoryForced = inireader.ReadInteger("Settings", "StreamMemoryForced", 0);
		if (streamMemoryForced > 0)
		{
			if (streamMemoryForced > MAX_MB)
			{
				streamMemoryForced = MAX_BYTE_LIMIT;
			}
			else {
				streamMemoryForced *= MULT_MB_TO_BYTE;
			}
			CPatch::SetUint32((uintptr_t)CStreaming::ms_memoryAvailable, streamMemoryForced);
		}
		removeUnusedWhenPercent = inireader.ReadFloat("Settings", "RemoveUnusedWhenPercent", 0.0f);
		removeUnusedIntervalMs = inireader.ReadInteger("Settings", "RemoveUnusedInterval", 60);
		CTimer::Stop();
		

		if (preLoadLODs && !FLAFix)
		{
			if (lods.size() > 0)
			{
				std::vector<uint16_t> lods_id(lods.size());
				std::transform(lods.begin(), lods.end(), lods_id.begin(), [](void* entity)
					{
						return *(uint16_t*)((uintptr_t)(entity) + 0x26); //Mode ID
					});
				// Load all lod models
				std::for_each(lods_id.begin(), std::unique(lods_id.begin(), lods_id.end()), [](uint16_t id) { CStreaming::RequestModel(id, eStreamingFlags::MISSION_REQUIRED); });
				CStreaming::LoadAllRequestedModels(false);

				std::for_each(lods.begin(), lods.end(), [](void* entity)
					{
					auto rwObject = *(void**)((uintptr_t)(entity) + 0x18); //RwObject
					if (rwObject == nullptr)
						Call::Method<void, void*>(library::GetSym("_ZN7CEntity14CreateRwObjectEv"), entity);
					});
			}
		}

		if (preLoadAnims)
		{
			int animBlocksIdStart = 0x63E7;
			for (int id = 1; id < *CAnimManager::ms_numAnimBlocks; ++id)
			{
				if (logMode >= 1)
				{
					lg << "Start loading anim " << id << endl;
				}
				CStreaming::RequestModel(id + animBlocksIdStart, eStreamingFlags::MISSION_REQUIRED);
				CAnimManager::AddAnimBlockRef(id);
			}
			CStreaming::LoadAllRequestedModels(false);
			if (logMode >= 1)
			{
				lg << "Finished loading anims." << endl;
			}		
		}
		
		int i = 0;
		while (true)
		{
			int loadEach = 0;
			int startId = -1;
			int endId = -1;
			int ignoreStart = -1;
			int ignoreEnd = -1;
			int biggerThan = -1;
			int smallerThan = -1;
			int ignorePedGroup = -1;
			bool keepLoaded = true;

			i++;
			string range = "Range" + to_string(i);
			loadEach = inireader.ReadInteger(range.data(), "LoadEach", 0);
			startId = inireader.ReadInteger(range.data(), "Start", -1);
			endId = inireader.ReadInteger(range.data(), "End", -1);
			ignoreStart = inireader.ReadInteger(range.data(), "IgnoreStart", -1);
			ignoreEnd = inireader.ReadInteger(range.data(), "IgnoreEnd", -1);
			biggerThan = inireader.ReadInteger(range.data(), "IfBiggerThan", -1);
			smallerThan = inireader.ReadInteger(range.data(), "IfSmallerThan", -1);
			ignorePedGroup = inireader.ReadInteger(range.data(), "IgnorePedGroup", -1) - 1;
			keepLoaded = inireader.ReadBoolean(range.data(), "KeepLoaded", 0) == true;


			if (startId <= 0 && endId <= 0) break;

			if (inireader.ReadBoolean(range.data(), "Enabled", 0) != 1) continue;
			if (logMode >= 0)
			{
				lg << "Start loading ID Range: " << i << "\n";
				lg.flush();
			}
			if (endId >= startId)
			{
				for (int model = startId; model <= endId; model++)
				{
					if ((ignoreStart <= 0 && ignoreEnd <= 0) || (model > ignoreEnd || model < ignoreStart))
					{
						if (CStreaming::ms_aInfoForModel[model].m_nCdSize != 0)
						{
							if ((biggerThan <= 0 && smallerThan <= 0) || (CStreaming::ms_aInfoForModel[model].m_nCdSize >= biggerThan && CStreaming::ms_aInfoForModel[model].m_nCdSize <= smallerThan))
							{
							    check_limit_to_load:
								if ((signed int)*CStreaming::ms_memoryUsed > (signed int)(*CStreaming::ms_memoryAvailable - 50000000))
								{
									if (*CStreaming::ms_memoryAvailable >= MAX_BYTE_LIMIT)
									{
										if (logMode >= 0) {
											lg << "ERROR: Not enough space\n";
										}	
									}
									else
									{
										if (streamMemoryForced > 0)
										{
											if (IncreaseStreamingMemoryLimit(256))
											{
												streamMemoryForced = *CStreaming::ms_memoryAvailable;
												if (logMode >= 0) {
													lg << "Streaming memory automatically increased to " << streamMemoryForced << " \n";
												}
												goto check_limit_to_load;
											}
										}
										else 
										{
											if (logMode >= 0) {
												lg << "ERROR: Not enough space. Try to increase the streaming memory.\n";
											}
										}
									}
									if (logMode >= 0) {
										lg.flush();
									}
									break;
								}
								else
								{
									//CPopCycle::IsPedInGroup lead to Crash when loading character models. we are not using
									if (ignorePedGroup > 0 /* && CPopCycle::IsPedInGroup(model, ignorePedGroup)*/) {
										if (logMode >= 1)
										{
											lg << "Model " << model << " is ignored. Pedgroup: " << ignorePedGroup << "\n";
											lg.flush();
										}
										continue;
									}
									if (logMode >= 1)
									{
										lg << "Loading " << model << " size " << CStreaming::ms_aInfoForModel[model].m_nCdSize << "\n";
										lg.flush();
									}
									if (i == 2) //car
										CStreaming::RequestModel(model, eStreamingFlags::KEEP_IN_MEMORY); //car Can only be loaded via KEEP_IN_MEMORY, otherwise it crashes. I have no specific research.
									else
										CStreaming::RequestModel(model, keepLoaded ? eStreamingFlags::MISSION_REQUIRED : eStreamingFlags::GAME_REQUIRED);
									if (*CStreaming::ms_numModelsRequested >= loadEach) CStreaming::LoadAllRequestedModels(false);
								}
							}
						}
					}
				}
				CStreaming::LoadAllRequestedModels(false);
			}
			if (logMode >= 0)
			{
				lg << "Finished loading ID Range: " << i << "\n";
				lg.flush();
			}
		}
		// last margin check
		if ((signed int)*CStreaming::ms_memoryUsed > (signed int)(*CStreaming::ms_memoryAvailable - 50000000))
		{
			if (IncreaseStreamingMemoryLimit(128)) {
				streamMemoryForced = *CStreaming::ms_memoryAvailable;
				if (logMode >= 0) {
					lg << "Streaming memory automatically increased to " << streamMemoryForced << " after loading (margin).\n";
				}
			}
		}

		if (logMode >= 0) {
			lg.flush();
		}
		CTimer::Update();
		gameStartedAfterLoad = *CTimer::m_snTimeInMilliseconds;

		loadCheck = 4;
	}
	
	if (loadCheck == 4)
	{
		if (removeUnusedWhenPercent > 0.0) {
			float memUsedPercent = (float)((float)*CStreaming::ms_memoryUsed / (float)*CStreaming::ms_memoryAvailable) * 100.0f;

			if (memUsedPercent >= removeUnusedWhenPercent) {
				// If memory usage is near limit, decrease the remove interval
				int removeUnusedIntervalMsTweaked = removeUnusedIntervalMs;
				if (memUsedPercent > 95.0f && removeUnusedIntervalMsTweaked > 0) {
					removeUnusedIntervalMsTweaked / 2;
				}
				if ((*CTimer::m_snTimeInMilliseconds - lastTimeRemoveUnused) > removeUnusedIntervalMsTweaked) {
					CStreaming::RemoveLeastUsedModel(0);
					lastTimeRemoveUnused = *CTimer::m_snTimeInMilliseconds;
				}
			}
		}
	}
	return;
}

void SAMImprovedStreaming()
{
	lg << "'SAMImprovedStreaming.so' init!!!\n";
	lg.flush();
	preLoadLODs = inireader.ReadInteger("Settings", "PreLoadLODs", 0) == 1;
	preLoadAnims = inireader.ReadInteger("Settings", "PreLoadAnims", 0) == 1;
	logMode = inireader.ReadInteger("Settings", "LogMode", -1);

	//PLT hook. Replace the got segment function pointerand keep the original function pointe
	CHook::PLTInternal((void*)(libs.pGame + 0x0066F2D0), (void*)MyinitRwEvent, (void**)&initRwEvent);
	CHook::PLTInternal((void*)(libs.pGame + 0x00671B14), (void*)MyinitScriptsEvent, (void**)&initScriptsEvent);
	CHook::PLTInternal((void*)(libs.pGame + 0x00672AAC), (void*)MyprocessScriptsEvent, (void**)&processScriptsEvent);
}

//use AML so loader____entry function
extern "C" void OnModLoad()
{
	if (dlopen("libplugin_fastman92limitAdjuster_ANDROID_ARM32.so", RTLD_LAZY))
	{
		inireader.SetIniPath("/storage/emulated/0/Android/data/com.rockstargames.gtasa/files/fastman92limitAdjuster_GTASA.ini");
		FLAFix = inireader.ReadBoolean("SPECIAL", "Disable CINFO.BIN and MINFO.BIN loading", 0);
	}
	lg.open(LOGPath, fstream::out | fstream::trunc);
	lg << "SAMImprovedStreaming v1.0, by XMDS||Junior_Djjr\n" << endl;
	inireader.SetIniPath(INIPath);
	SAMImprovedStreaming();
}