#pragma once
#include <unistd.h>
#include "CLEO_SDK/cleo.h"
#include "ARMHook/CHook.h"
#include "ARMHook/Call.h"

using namespace ARMHook;
cleo_ifs_t* cleo = nullptr;

class CPopCycle
{
public:
	static bool IsPedInGroup(int modelIndex, int PopCycle_Group)
	{
		return Call::Function<bool, int, int>((uintptr_t)cleo->GetMainLibrarySymbol("_ZN9CPopCycle12IsPedInGroupEii"), modelIndex, PopCycle_Group);
	}
};

class CTimer
{
public:
	static void Stop()
	{
		return Call::Function<void>((uintptr_t)cleo->GetMainLibrarySymbol("_ZN6CTimer4StopEv"));
	}

	static void Update()
	{
		return Call::Function<void>((uintptr_t)cleo->GetMainLibrarySymbol("_ZN6CTimer6UpdateEv"));
	}
};

class CStreamingInfo
{
public:
	short m_nNextIndex; // ms_pArrayBase array index
	short m_nPrevIndex; // ms_pArrayBase array index
	short m_nNextIndexOnCd;
	unsigned char m_nFlags; // see eStreamingFlags
	unsigned char m_nImgId;
	unsigned int m_nCdPosn;
	unsigned int m_nCdSize;
	unsigned char m_nLoadState; // see eStreamingLoadState
private:
	char  __pad[3];
};

enum eStreamingFlags {
	GAME_REQUIRED = 0x2,
	MISSION_REQUIRED = 0x4,
	KEEP_IN_MEMORY = 0x8,
	PRIORITY_REQUEST = 0x10
};

class CStreaming
{
public:
	static CStreamingInfo* ms_aInfoForModel;
	
	static void RequestModel(int dwModelId, int Streamingflags)
	{
		return Call::Function<void, int, int>((uintptr_t)cleo->GetMainLibrarySymbol("_ZN10CStreaming12RequestModelEii"), dwModelId, Streamingflags);
	}

	static void LoadAllRequestedModels(bool bOnlyPriorityRequests)
	{
		return Call::Function<void, bool>((uintptr_t)cleo->GetMainLibrarySymbol("_ZN10CStreaming22LoadAllRequestedModelsEb"), bOnlyPriorityRequests);
	}

	static bool RemoveLeastUsedModel(unsigned int StreamingFlags)
	{
		return Call::Function<bool, unsigned int>((uintptr_t)cleo->GetMainLibrarySymbol("_ZN10CStreaming20RemoveLeastUsedModelEj"), StreamingFlags);
	}
};		

class CAnimManager
{
public:
	static void AddAnimBlockRef(int index)
	{
		return Call::Function<void, int>((uintptr_t)cleo->GetMainLibrarySymbol("_ZN12CAnimManager15AddAnimBlockRefEi"), index);
	}
};