#include "plugin.h"
#include "..\injector\assembly.hpp"
#include "CStreaming.h"
#include "IniReader/IniReader.h"
#include "CIplStore.h"
#include "extensions/ScriptCommands.h"
#include "CMessages.h"
#include "CTimer.h"
#include "CPopCycle.h"
#include <vector>

using namespace plugin;
using namespace std;
using namespace injector;

class LoadWholeMap {
public:

    LoadWholeMap() {

		static int totalBinaryIPLconfig = 0;
		static int totalBinaryIPLloaded = 0;

		static int loadCheck = 0;
		static int streamMemoryForced = 0;
		static fstream lg;

		lg.open("LoadWholeMap.log", fstream::out | fstream::trunc);
		lg << "v2.2\n";
		lg.flush();

		static CIniReader ini("LoadWholeMap.ini");
		static bool loadBinaryIPLs = ini.ReadInteger("Settings", "LoadBinaryIPLs", 0) == 1;

		Events::initRwEvent += []
		{
			if (loadBinaryIPLs)
			{
				static std::vector<std::string> IPLStreamNames;

				totalBinaryIPLconfig = 0;
				ifstream stream("LoadWholeMap_BinaryIPLs.dat");
				for (string line; getline(stream, line); ) {
					if (line[0] != ';' && line[0] != '#') {
						while (getline(stream, line) && line.compare("end")) {
							if (line[0] != ';' && line[0] != '#') {
								char name[32];
								int loadWhen; // not used anymore
								if (sscanf(line.c_str(), "%s %i", name, &loadWhen) >= 1)
								{
									IPLStreamNames.push_back(name);
								}
							}
						}
					}
				}

				// by ThirteenAG
				struct LoadAllBinaryIPLs
				{
					void operator()(injector::reg_pack&)
					{
						static auto CIplStoreLoad = (char *(__cdecl *)()) 0x5D54A0;
						CIplStoreLoad();

						static auto IplFilePoolLocate = (int(__cdecl *)(const char *name)) 0x404AC0;
						static auto CIplStoreRequestIplAndIgnore = (char *(__cdecl *)(int a1)) 0x405850;

						injector::address_manager::singleton().IsHoodlum() ?
							injector::WriteMemory<char>(0x015651C1 + 3, 0, true) :
							injector::WriteMemory<char>(0x405881 + 3, 0, true);

						for (auto it = IPLStreamNames.cbegin(); it != IPLStreamNames.cend(); it++)
						{
							lg << "Loading IPL " << (string)*it << "\n";
							lg.flush();
							CIplStoreRequestIplAndIgnore(IplFilePoolLocate(it->c_str()));
						}

						injector::address_manager::singleton().IsHoodlum() ?
							injector::WriteMemory<char>(0x015651C1 + 3, 1, true) :
							injector::WriteMemory<char>(0x405881 + 3, 1, true);
					}
				}; injector::MakeInline<LoadAllBinaryIPLs>(0x5D19A4);
			}
		};

		// ---------------------------------------------------

		Events::initScriptsEvent.after += []
		{
			loadCheck = 1;
		};

		Events::processScriptsEvent.after += []
		{
			if (loadCheck < 3) // ignore first thicks
			{
				loadCheck++;
				return;
			}

			if (streamMemoryForced > 0) {
				injector::WriteMemory<uint32_t>(0x8A5A80, streamMemoryForced, false); // CStreaming::ms_memoryAvailable = streamMemoryForced;lg << "Loading " << name << "\n";
			}

			/*if (totalBinaryIPLconfig > 0 && totalBinaryIPLloaded < totalBinaryIPLconfig)
			{
				for (unsigned int i = 0; i < GetBinaryIPLconfigVector().size(); i++) {
					if (GetBinaryIPLconfigVector()[i].loaded == false && CTimer::m_snTimeInMilliseconds > GetBinaryIPLconfigVector()[i].loadWhen)
					{
						CIplStore::RequestIplAndIgnore(GetBinaryIPLconfigVector()[i].slot);
						GetBinaryIPLconfigVector()[i].loaded = true;
						totalBinaryIPLloaded++;
					}
				}
			}*/

			if (loadCheck == 3)
			{
				if (!(GetKeyState(0x10) & 0x8000)) // SHIFT
				{

					bool logAll = ini.ReadInteger("Settings", "LogAll", 0) == 1;
					

					streamMemoryForced = ini.ReadInteger("Settings", "StreamMemoryForced", 0);
					if (streamMemoryForced > 0)
					{
						if (streamMemoryForced > 2047) {
							streamMemoryForced = 2147483647;
						}
						else {
							streamMemoryForced *= 1048576;
						}
						injector::WriteMemory<uint32_t>(0x8A5A80, streamMemoryForced, false);
					}

					CTimer::Stop();

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

						i++;

						string range = "Range" + to_string(i);

						loadEach = ini.ReadInteger(range, "LoadEach", 0);
						startId = ini.ReadInteger(range, "Start", -1);
						endId = ini.ReadInteger(range, "End", -1);
						ignoreStart = ini.ReadInteger(range, "IgnoreStart", -1);
						ignoreEnd = ini.ReadInteger(range, "IgnoreEnd", -1);
						biggerThan = ini.ReadInteger(range, "IfBiggerThan", -1);
						smallerThan = ini.ReadInteger(range, "IfSmallerThan", -1);
						ignorePedGroup = ini.ReadInteger(range, "IgnorePedGroup", -1) - 1;

						if (startId <= 0 && endId <= 0) break;

						if (ini.ReadInteger(range, "Enabled", 0) != 1) continue;

						lg << "Loading ID Range: " << i << "\n";
						lg.flush();

						if (endId >= startId)
						{
							for (int model = startId; model <= endId; model++)
							{
								if (GetKeyState(0x10) & 0x8000) break; // SHIFT
								if ((ignoreStart <= 0 && ignoreEnd <= 0) || (model > ignoreEnd || model < ignoreStart))
								{
									if (CStreaming::ms_aInfoForModel[model].m_nCdSize != 0)
									{
										if ((biggerThan <= 0 && smallerThan <= 0) || (CStreaming::ms_aInfoForModel[model].m_nCdSize >= biggerThan && CStreaming::ms_aInfoForModel[model].m_nCdSize <= smallerThan))
										{
											if ((signed int)CStreaming::ms_memoryUsed > (signed int)(CStreaming::ms_memoryAvailable - 50000000))
											{
												if (CStreaming::ms_memoryAvailable == 2147483647)
												{
													lg << "ERROR: Not enough space\n";
													CMessages::AddMessageJumpQ("~r~ERROR Load Whole Map: Not enough space. Try to disable some ranges, configure or use other settings.", 6000, false, false);
												}
												else {
													lg << "ERROR: Not enough space. Try to increase the streaming memory.\n";
													CMessages::AddMessageJumpQ("~r~ERROR Load Whole Map: Not enough space. Try to increase the streaming memory.", 6000, false, false);
												}
												lg.flush();
												break;
											}
											else
											{
												if (ignorePedGroup > 0 && CPopCycle::IsPedInGroup(model, ignorePedGroup)) {
													if (logAll)
													{
														lg << "Model " << model << " is ignored. Pedgroup: " << ignorePedGroup << "\n";
														lg.flush();
													}
													continue;
												}
												if (logAll)
												{
													lg << "Loading " << model << " size " << CStreaming::ms_aInfoForModel[model].m_nCdSize << "\n";
													lg.flush();
												}
												CStreaming::RequestModel(model, eStreamingFlags::KEEP_IN_MEMORY);
												if (CStreaming::ms_numModelsRequested >= loadEach) CStreaming::LoadAllRequestedModels(false);
											}
										}
									}
								}
							}
							CStreaming::LoadAllRequestedModels(false);
						}
					}


					CTimer::Update();

				}

				loadCheck = 4;
			}
		};

    }

} loadWholeMap;
