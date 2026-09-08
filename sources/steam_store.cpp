#include "steam_store.h"

#include "core/as_string.h"
#include "core/log.h"
#include "core/file_logger.h"
#include "core/application.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifndef AS1_WITH_STEAM
#define AS1_WITH_STEAM 1
#endif

#if AS1_WITH_STEAM
namespace as1 { namespace steam
{
    namespace
    {
        constexpr std::size_t kMaximumLeaderboards = 32;
        constexpr std::size_t kMaximumDownloadedEntries = 64;

        bool g_initialized = false;
        bool g_alive = false;
        bool g_coreApiInitialized = false;
        bool g_statsDirty = false;

        std::array<STRING, kMaximumLeaderboards> g_leaderboardNames{};
        std::array<std::uint64_t, kMaximumLeaderboards> g_leaderboardHandles{};
        std::array<STRING, kMaximumDownloadedEntries> g_downloadedNames{};
        std::array<int, kMaximumDownloadedEntries> g_downloadedScores{};

#ifdef _WIN32
        extern "C"
        {
            __declspec(dllimport) void __cdecl SteamAPI_RunCallbacks();
            __declspec(dllimport) bool __cdecl SteamAPI_RestartAppIfNecessary(std::uint32_t appId);
            __declspec(dllimport) void __cdecl SteamAPI_Shutdown();
            __declspec(dllimport) bool __cdecl SteamAPI_Init();
            __declspec(dllimport) void __cdecl SteamAPI_RegisterCallResult(void* callback, std::uint64_t apiCall);
            __declspec(dllimport) void __cdecl SteamAPI_UnregisterCallResult(void* callback, std::uint64_t apiCall);
            __declspec(dllimport) int __cdecl SteamAPI_GetHSteamPipe();
            __declspec(dllimport) int __cdecl SteamAPI_GetHSteamUser();
            __declspec(dllimport) void* __cdecl SteamInternal_ContextInit(void* contextInitData);
            __declspec(dllimport) void* __cdecl SteamInternal_CreateInterface(const char* version);
        }

        struct SteamContext
        {
            void* client = nullptr;             // +0x00
            void* user = nullptr;               // +0x04
            void* friends = nullptr;            // +0x08
            void* utils = nullptr;              // +0x0C
            void* matchMaking = nullptr;        // +0x10
            void* userStats = nullptr;          // +0x14
            void* apps = nullptr;               // +0x18
            void* matchMakingServers = nullptr; // +0x1C
            void* networking = nullptr;         // +0x20
            void* remoteStorage = nullptr;      // +0x24
            void* screenshots = nullptr;        // +0x28
            void* http = nullptr;               // +0x2C
            void* unifiedMessages = nullptr;    // +0x30
            void* controller = nullptr;         // +0x34
            void* ugc = nullptr;                // +0x38
            void* appList = nullptr;            // +0x3C
            void* music = nullptr;              // +0x40
            void* musicRemote = nullptr;        // +0x44
            void* htmlSurface = nullptr;        // +0x48
            void* inventory = nullptr;          // +0x4C
            void* video = nullptr;              // +0x50
        };
#if defined(_M_IX86)
                                                                                    
#endif

        template <typename Ret, typename... Args>
        Ret steamVCall(void* object, std::size_t byteOffset, Args... args)
        {
            using Fn = Ret (__thiscall*)(void*, Args...);
            void** const vtable = *reinterpret_cast<void***>(object);
            return reinterpret_cast<Fn>(vtable[byteOffset / sizeof(void*)])(object, args...);
        }

        void initializeSteamContext(void* rawContext)
        {
            auto& context = *static_cast<SteamContext*>(rawContext);
            std::memset(&context, 0, sizeof(context));

            const int userHandle = SteamAPI_GetHSteamUser();
            const int pipeHandle = SteamAPI_GetHSteamPipe();
            if (!pipeHandle)
                return;

            context.client = SteamInternal_CreateInterface("SteamClient017");
            if (!context.client) return;
            context.user = steamVCall<void*>(context.client, 20, userHandle, pipeHandle, "SteamUser019");
            if (!context.user) return;
            context.friends = steamVCall<void*>(context.client, 32, userHandle, pipeHandle, "SteamFriends015");
            if (!context.friends) return;
            context.utils = steamVCall<void*>(context.client, 36, pipeHandle, "SteamUtils009");
            if (!context.utils) return;
            context.matchMaking = steamVCall<void*>(context.client, 40, userHandle, pipeHandle, "SteamMatchMaking009");
            if (!context.matchMaking) return;
            context.matchMakingServers = steamVCall<void*>(context.client, 44, userHandle, pipeHandle, "SteamMatchMakingServers002");
            if (!context.matchMakingServers) return;
            context.userStats = steamVCall<void*>(context.client, 52, userHandle, pipeHandle, "STEAMUSERSTATS_INTERFACE_VERSION011");
            if (!context.userStats) return;
            context.apps = steamVCall<void*>(context.client, 60, userHandle, pipeHandle, "STEAMAPPS_INTERFACE_VERSION008");
            if (!context.apps) return;
            context.networking = steamVCall<void*>(context.client, 64, userHandle, pipeHandle, "SteamNetworking005");
            if (!context.networking) return;
            context.remoteStorage = steamVCall<void*>(context.client, 68, userHandle, pipeHandle, "STEAMREMOTESTORAGE_INTERFACE_VERSION014");
            if (!context.remoteStorage) return;
            context.screenshots = steamVCall<void*>(context.client, 72, userHandle, pipeHandle, "STEAMSCREENSHOTS_INTERFACE_VERSION003");
            if (!context.screenshots) return;
            context.http = steamVCall<void*>(context.client, 92, userHandle, pipeHandle, "STEAMHTTP_INTERFACE_VERSION002");
            if (!context.http) return;
            context.unifiedMessages = steamVCall<void*>(context.client, 96, userHandle, pipeHandle, "STEAMUNIFIEDMESSAGES_INTERFACE_VERSION001");
            if (!context.unifiedMessages) return;
            context.controller = steamVCall<void*>(context.client, 100, userHandle, pipeHandle, "SteamController005");
            if (!context.controller) return;
            context.ugc = steamVCall<void*>(context.client, 104, userHandle, pipeHandle, "STEAMUGC_INTERFACE_VERSION010");
            if (!context.ugc) return;
            context.appList = steamVCall<void*>(context.client, 108, userHandle, pipeHandle, "STEAMAPPLIST_INTERFACE_VERSION001");
            if (!context.appList) return;
            context.music = steamVCall<void*>(context.client, 112, userHandle, pipeHandle, "STEAMMUSIC_INTERFACE_VERSION001");
            if (!context.music) return;
            context.musicRemote = steamVCall<void*>(context.client, 116, userHandle, pipeHandle, "STEAMMUSICREMOTE_INTERFACE_VERSION001");
            if (!context.musicRemote) return;
            context.htmlSurface = steamVCall<void*>(context.client, 120, userHandle, pipeHandle, "STEAMHTMLSURFACE_INTERFACE_VERSION_003");
            if (!context.htmlSurface) return;
            context.inventory = steamVCall<void*>(context.client, 136, userHandle, pipeHandle, "STEAMINVENTORY_INTERFACE_V002");
            if (!context.inventory) return;
            context.video = steamVCall<void*>(context.client, 140, userHandle, pipeHandle, "STEAMVIDEO_INTERFACE_V002");
        }

        struct SteamContextInitData
        {
            void (__cdecl* init)(void*) = &initializeSteamContext;
            std::uint32_t callbackCounter = 0;
            SteamContext context{};
        };

        SteamContextInitData g_contextInitData{};
        SteamContext* g_context = nullptr;
        void* g_stats = nullptr;
        void* g_friends = nullptr;

#pragma pack(push, 8)
        struct LeaderboardFindResult
        {
            std::uint64_t leaderboard;
            std::uint8_t found;
        };

        struct LeaderboardScoresDownloaded
        {
            std::uint64_t leaderboard;
            std::uint64_t entries;
            std::int32_t entryCount;
        };

        struct LeaderboardScoreUploaded
        {
            std::uint8_t success;
            std::uint64_t leaderboard;
            std::int32_t score;
            std::uint8_t scoreChanged;
            std::int32_t globalRankNew;
            std::int32_t globalRankPrevious;
        };

        struct LeaderboardEntry
        {
            std::uint64_t steamId;
            std::int32_t globalRank;
            std::int32_t score;
            std::int32_t detailsCount;
            std::uint64_t ugc;
        };
#pragma pack(pop)

                                                                                                     
                                                                                                                 
                                                                                                           
                                                                                           

        class LeaderboardState;

        class SteamCallbackBase
        {
        public:
            explicit SteamCallbackBase(int callbackId) noexcept
                : m_flags(0), m_callbackId(callbackId)
            {
            }

            virtual void Run(void* parameter) = 0;
            virtual void Run(void* parameter, bool ioFailure, std::uint64_t apiCall) = 0;
            virtual int GetCallbackSizeBytes() = 0;

        protected:
            std::uint8_t m_flags;
            std::int32_t m_callbackId;
        };

        template <typename Parameter, int CallbackId>
        class SteamCallResult final : public SteamCallbackBase
        {
        public:
            using Handler = void (LeaderboardState::*)(Parameter*, bool);

            SteamCallResult() noexcept
                : SteamCallbackBase(CallbackId)
            {
            }

            ~SteamCallResult()
            {
                Cancel();
            }

            void Set(std::uint64_t apiCall, LeaderboardState* owner, Handler handler)
            {
                Cancel();
                m_apiCall = apiCall;
                m_owner = owner;
                m_handler = handler;
                if (m_apiCall)
                    SteamAPI_RegisterCallResult(this, m_apiCall);
            }

            void Cancel() noexcept
            {
                if (m_apiCall)
                    SteamAPI_UnregisterCallResult(this, m_apiCall);
                m_apiCall = 0;
            }

            void Run(void* parameter) override
            {
                LeaderboardState* const owner = m_owner;
                Handler const handler = m_handler;
                m_apiCall = 0;
                if (owner && handler)
                    (owner->*handler)(static_cast<Parameter*>(parameter), false);
            }

            void Run(void* parameter, bool ioFailure, std::uint64_t apiCall) override
            {
                if (apiCall != m_apiCall)
                    return;
                LeaderboardState* const owner = m_owner;
                Handler const handler = m_handler;
                m_apiCall = 0;
                if (owner && handler)
                    (owner->*handler)(static_cast<Parameter*>(parameter), ioFailure);
            }

            int GetCallbackSizeBytes() override
            {
                return static_cast<int>(sizeof(Parameter));
            }

        private:
            std::uint64_t m_apiCall = 0;
            LeaderboardState* m_owner = nullptr;
            Handler m_handler = nullptr;
        };

        std::array<LeaderboardEntry, kMaximumDownloadedEntries> g_downloadedEntries{};
        int g_downloadEntryCount = -1;

        class LeaderboardState
        {
        public:
            SteamCallResult<LeaderboardFindResult, 0x450> findResult;
            SteamCallResult<LeaderboardScoreUploaded, 0x452> uploadResult;
            SteamCallResult<LeaderboardScoresDownloaded, 0x451> downloadResult;
            std::int32_t reserved60 = 0;
            std::int32_t findBusy = 0;
            std::uint8_t downloadBusy = 0;
            std::uint8_t downloadIoFailure = 0;
            std::uint16_t reserved6A = 0;
            std::int32_t lastUploadRank = 0;

            void CancelCallResults() noexcept
            {
                findResult.Cancel();
                uploadResult.Cancel();
                downloadResult.Cancel();
                findBusy = 0;
                downloadBusy = 0;
                downloadIoFailure = 0;
            }

            void StartNextFind()
            {
                if (findBusy || !g_stats)
                    return;

                for (std::size_t i = 0; i < kMaximumLeaderboards; ++i)
                {
                    if (g_leaderboardHandles[i] != 0 || g_leaderboardNames[i].isEmpty())
                        continue;

                    const std::uint64_t apiCall = steamVCall<std::uint64_t>(g_stats, 92, g_leaderboardNames[i].c_str());
                    if (apiCall)
                    {
                        findResult.Set(apiCall, this, &LeaderboardState::OnFindLeaderboard);
                        findBusy = 1;
                    }
                    return;
                }
            }

            void OnFindLeaderboard(LeaderboardFindResult* result, bool ioFailure)
            {
                findBusy = 0;
                if (!result || !result->found || ioFailure || !g_stats)
                {
                    const char* failedName = "";
                    if (result && g_stats)
                        failedName = steamVCall<const char*>(g_stats, 96, result->leaderboard);
                    writeLogLine(g_fileLogger, "!!!ERROR!!!Can't found leaderboard '%s', %i",
                                 failedName ? failedName : "", ioFailure ? 1 : 0);
                    return;
                }

                const char* const name = steamVCall<const char*>(g_stats, 96, result->leaderboard);
                if (name)
                {
                    for (std::size_t i = 0; i < kMaximumLeaderboards; ++i)
                    {
                        if (std::strcmp(g_leaderboardNames[i].c_str(), name) == 0)
                            g_leaderboardHandles[i] = result->leaderboard;
                    }
                }
                StartNextFind();
            }

            void OnDownload(LeaderboardScoresDownloaded* result, bool ioFailure)
            {
                downloadBusy = 0;
                downloadIoFailure = ioFailure ? 1u : 0u;
                if (!result || !g_stats)
                    return;

                int entryCount = g_downloadEntryCount;
                if (entryCount < 0)
                {
                    entryCount = -entryCount;
                    if (result->entryCount < entryCount)
                        entryCount = result->entryCount;
                    g_downloadEntryCount = entryCount;
                }

                const int safeCount = std::min(entryCount, static_cast<int>(kMaximumDownloadedEntries));
                for (int i = 0; i < safeCount; ++i)
                {
                    (void)steamVCall<bool>(g_stats,
                                           120,
                                           result->entries,
                                           i,
                                           &g_downloadedEntries[static_cast<std::size_t>(i)],
                                           static_cast<std::int32_t*>(nullptr),
                                           0);
                }
            }

            void OnUpload(LeaderboardScoreUploaded* result, bool)
            {
                if (result && result->success)
                {
                    lastUploadRank = result->globalRankNew;
                }
                else
                {
                    lastUploadRank = -2;
                    const char* leaderboardName = "";
                    int uploadedScore = 0;
                    if (result)
                    {
                        uploadedScore = result->score;
                        if (g_stats)
                            leaderboardName = steamVCall<const char*>(g_stats, 96, result->leaderboard);
                    }
                    writeLogLine(g_fileLogger, "!!!ERROR!!!Can't upload leaderboard(%s) %i",
                                 leaderboardName ? leaderboardName : "", uploadedScore);
                }
            }
        };

        LeaderboardState g_leaderboards{};

#if defined(_M_IX86)
                                                                                                                     
                                                                                                           
                                                                                                                   
                                                                                                                   
                                                                                                             
#endif

        struct RetailCodepointMap
        {
            std::uint16_t codepoint;
            std::uint8_t byte;
        };

        constexpr RetailCodepointMap kRetailCodepointMap[] = {
            {0x201A,0x82},{0x0453,0x83},{0x201E,0x84},{0x2026,0x85},
            {0x2020,0x86},{0x2021,0x87},{0x20AC,0x88},{0x2030,0x89},
            {0x0409,0x8A},{0x2039,0x8B},{0x040A,0x8C},{0x040C,0x8D},
            {0x040B,0x8E},{0x040F,0x8F},{0x0452,0x90},{0x2018,0x91},
            {0x2019,0x92},{0x201C,0x93},{0x201D,0x94},{0x2022,0x95},
            {0x2013,0x96},{0x2014,0x97},{0x2122,0x99},{0x0459,0x9A},
            {0x203A,0x9B},{0x045A,0x9C},{0x045C,0x9D},{0x045B,0x9E},
            {0x045F,0x9F},{0x00A0,0xA0},{0x040E,0xA1},{0x045E,0xA2},
            {0x0408,0xA3},{0x00A4,0xA4},{0x0490,0xA5},{0x00A6,0xA6},
            {0x00A7,0xA7},{0x0401,0xA8},{0x00A9,0xA9},{0x0404,0xAA},
            {0x00AB,0xAB},{0x00AC,0xAC},{0x00AD,0xAD},{0x00AE,0xAE},
            {0x0407,0xAF},{0x00B0,0xB0},{0x00B1,0xB1},{0x0406,0xB2},
            {0x0456,0xB3},{0x0491,0xB4},{0x00B5,0xB5},{0x00B6,0xB6},
            {0x00B7,0xB7},{0x0451,0xB8},{0x2116,0xB9},{0x0454,0xBA},
            {0x00BB,0xBB},{0x0458,0xBC},{0x0405,0xBD},{0x0455,0xBE},
            {0x0457,0xBF}
        };

        bool retailSingleByteForCodepoint(std::uint16_t codepoint, std::uint8_t& output) noexcept
        {
            if (codepoint >= 0x0410 && codepoint <= 0x044F)
            {
                output = static_cast<std::uint8_t>(codepoint - 0x0350);
                return true;
            }
            if (codepoint >= 0x0080 && codepoint <= 0x00FF)
            {
                output = static_cast<std::uint8_t>(codepoint);
                return true;
            }
            if (codepoint == 0x0402 || codepoint == 0x0403)
            {
                output = static_cast<std::uint8_t>(codepoint + 0x007E);
                return true;
            }
            for (const auto& item : kRetailCodepointMap)
            {
                if (item.codepoint == codepoint)
                {
                    output = item.byte;
                    return true;
                }
            }
            return false;
        }

        STRING convertPersonaNameRetail(const char* input)
        {
            if (!input)
                return STRING("");

            const std::size_t length = std::strlen(input);
            if (length >= 512)
                return STRING(input);

            char converted[512]{};
            std::size_t source = 0;
            std::size_t destination = 0;
            while (source < length)
            {
                const std::uint8_t first = static_cast<std::uint8_t>(input[source]);
                if ((first & 0x80u) == 0)
                {
                    converted[destination++] = static_cast<char>(first);
                    ++source;
                    continue;
                }

                if ((first & 0xE0u) != 0xC0u || source + 1 >= length)
                    return STRING(input);
                const std::uint8_t second = static_cast<std::uint8_t>(input[source + 1]);
                if ((second & 0xC0u) != 0x80u)
                    return STRING(input);

                const std::uint16_t codepoint = static_cast<std::uint16_t>(((first & 0x1Fu) << 6) | (second & 0x3Fu));
                std::uint8_t mapped = 0;
                if (!retailSingleByteForCodepoint(codepoint, mapped))
                    return STRING(input);
                converted[destination++] = static_cast<char>(mapped);
                source += 2;
            }
            converted[destination] = 0;
            return STRING(converted);
        }

        void __cdecl steamWarningHook(int, const char* text)
        {
            ::OutputDebugStringA(text);
        }

        void resetInterfacePointers() noexcept
        {
            g_context = nullptr;
            g_stats = nullptr;
            g_friends = nullptr;
        }

        void resetStoreState()
        {
            g_statsDirty = false;
            g_downloadEntryCount = -1;
            g_leaderboards.CancelCallResults();
            g_leaderboards.lastUploadRank = 0;
            g_leaderboardHandles.fill(0);
            g_downloadedScores.fill(0);
            for (STRING& name : g_leaderboardNames)
                name = "";
            for (STRING& name : g_downloadedNames)
                name = "";
        }

        int findLeaderboardIndex(const char* name) noexcept
        {
            if (!name)
                return -1;
            for (std::size_t i = 0; i < kMaximumLeaderboards; ++i)
            {
                if (std::strcmp(g_leaderboardNames[i].c_str(), name) == 0)
                    return static_cast<int>(i);
            }
            return -1;
        }

        bool attachSteam(int appId)
        {
            resetInterfacePointers();
            g_coreApiInitialized = false;

            if (SteamAPI_RestartAppIfNecessary(static_cast<std::uint32_t>(appId)))
            {
                return false;
            }

            if (!SteamAPI_Init())
            {
                ::OutputDebugStringA("SteamAPI_Init() failed\n");
                ::MessageBoxA(nullptr,
                              "Steam must be running to play this game (SteamAPI_Init() failed).\n",
                              "Fatal Error", MB_OK);
                return false;
            }
            g_coreApiInitialized = true;

            g_context = static_cast<SteamContext*>(SteamInternal_ContextInit(&g_contextInitData));
            if (!g_context)
            {
                return false;
            }

            if (g_context->client)
                steamVCall<void>(g_context->client, 84, &steamWarningHook);

            if (!g_context->user || !steamVCall<bool>(g_context->user, 4))
            {
                ::OutputDebugStringA("Steam user is not logged in\n");
                ::MessageBoxA(nullptr,
                              "Steam user must be logged in to play this game (SteamUser()->BLoggedOn() returned false).\n",
                              "Fatal Error", MB_OK);
                return false;
            }

            if (!g_context->controller || !steamVCall<bool>(g_context->controller, 0))
            {
                ::OutputDebugStringA("SteamController()->Init failed.\n");
                ::MessageBoxA(nullptr, "SteamController()->Init failed.\n", "Fatal Error", MB_OK);
                return false;
            }

            g_stats = g_context->userStats;
            g_friends = g_context->friends;
            if (g_stats && !steamVCall<bool>(g_stats, 0))
                writeLogLine(g_fileLogger, "!!!ERROR!!!STORE::RequestCurrentStats");

            return true;
        }
#endif
    }

    bool Initialize(int appId)
    {
        if (g_initialized)
            return g_alive;

        g_initialized = true;
        g_alive = false;
#ifdef _WIN32
        resetStoreState();
        g_alive = attachSteam(appId);
#endif
        return g_alive;
    }

    void Pump()
    {
#ifdef _WIN32
        SteamAPI_RunCallbacks();
#endif
    }

    void Shutdown()
    {
#ifdef _WIN32
        if (g_coreApiInitialized)
        {
            if (g_stats && g_statsDirty)
                (void)steamVCall<bool>(g_stats, 40);
            SteamAPI_Shutdown();
        }
        g_coreApiInitialized = false;
        resetInterfacePointers();
#endif
        g_alive = false;
        g_initialized = false;
    }

    bool Alive() noexcept { return g_alive; }

    void SetAchievement(const char* id)
    {
        if (!id)
            return;
#ifdef _WIN32
        if (g_alive && g_stats)
        {
            g_statsDirty = true;
            if (!steamVCall<bool>(g_stats, 28, id))
                writeLogLine(g_fileLogger, "!!!ERROR!!!STORE::SetAchievement %s", id);
        }
#else
        (void)id;
#endif
    }

    int GetAchievement(const char* id)
    {
        if (!id)
            return 0;
#ifdef _WIN32
        bool value = false;
        if (g_alive && g_stats && !steamVCall<bool>(g_stats, 24, id, &value))
            writeLogLine(g_fileLogger, "!!!ERROR!!!STORE::GetAchievement %s", id);
        return value ? 1 : 0;
#else
        return 0;
#endif
    }

    void ClearAchievement(const char* id)
    {
        if (!id)
            return;
#ifdef _WIN32
        if (g_alive && g_stats)
        {
            g_statsDirty = true;
            if (!steamVCall<bool>(g_stats, 28, id))
                writeLogLine(g_fileLogger, "!!!ERROR!!!STORE::ClearAchievement %s", id);
        }
#else
        (void)id;
#endif
    }

    void ResetAllStats()
    {
#ifdef _WIN32
        if (g_alive && g_stats)
        {
            g_statsDirty = true;
            if (!steamVCall<bool>(g_stats, 84, true))
                writeLogLine(g_fileLogger, "!!!ERROR!!!STORE::ResetAllStats");
        }
#endif
    }

    void SetStat(const char* id, int value)
    {
        if (!id)
            return;
#ifdef _WIN32
        if (g_alive && g_stats)
        {
            g_statsDirty = true;
            if (!steamVCall<bool>(g_stats, 16, id, static_cast<std::int32_t>(value)))
                writeLogLine(g_fileLogger, "!!!ERROR!!!STORE::SetStat %s", id);
        }
#else
        (void)value;
#endif
    }

    int GetStat(const char* id)
    {
        if (!id)
            return 0;
#ifdef _WIN32
        std::int32_t value = 0;
        if (g_alive && g_stats && !steamVCall<bool>(g_stats, 8, id, &value))
            writeLogLine(g_fileLogger, "!!!ERROR!!!STORE::GetStat %s", id);
        return static_cast<int>(value);
#else
        return 0;
#endif
    }

    void SaveStatsIfNeeded()
    {
#ifdef _WIN32
        if (g_alive && g_stats && g_statsDirty)
            (void)steamVCall<bool>(g_stats, 40);
#endif
    }

    void ActivateStore(int appId)
    {
        if (appId <= 0)
            appId = AppId;
#ifdef _WIN32
        if (g_alive && g_friends)
            (void)steamVCall<void>(g_friends, 124, static_cast<std::uint32_t>(appId), 0);
#else
        (void)appId;
#endif
    }

    void InitLeaderboards(const char* names)
    {
#ifdef _WIN32
        if (!names)
            names = "";

        const char* segment = names;
        std::size_t index = 0;
        for (const char* cursor = names;; ++cursor)
        {
            if (*cursor != '#' && *cursor != '\0')
                continue;

            if (index >= kMaximumLeaderboards)
            {
                writeLogLine(g_fileLogger,
                             "!!!ERROR!!!Can't init leaderboard '%s'. (Noleaderboard >= Maximum(%i))",
                             names, static_cast<int>(kMaximumLeaderboards));
                g_leaderboards.StartNextFind();
                return;
            }

            g_leaderboardNames[index].AssignBytes(segment, static_cast<std::size_t>(cursor - segment));
            ++index;
            if (*cursor == '\0')
                break;
            segment = cursor + 1;
        }

        g_leaderboards.StartNextFind();
#else
        (void)names;
#endif
    }

    void UpdateLeaderboard(const char* name, int score)
    {
#ifdef _WIN32
        const int index = findLeaderboardIndex(name);
        if (index < 0)
        {
            writeLogLine(g_fileLogger,
                         "!!!ERROR!!!Can't find leaderboard for UpdateLeaderboards(%s, %i)",
                         name ? name : "", score);
            return;
        }

        const std::uint64_t leaderboard = g_leaderboardHandles[static_cast<std::size_t>(index)];
        if (!leaderboard || !g_stats)
        {
            writeLogLine(g_fileLogger,
                         "!!!ERROR!!!Not loaded leaderboard for UpdateLeaderboards(%s, %i)",
                         name ? name : "", score);
            return;
        }

        g_leaderboards.lastUploadRank = -1;
        const std::uint64_t apiCall = steamVCall<std::uint64_t>(g_stats,
                                                                124,
                                                                leaderboard,
                                                                1,
                                                                score,
                                                                static_cast<const std::int32_t*>(nullptr),
                                                                0);
        g_leaderboards.uploadResult.Set(apiCall, &g_leaderboards, &LeaderboardState::OnUpload);
#else
        (void)name;
        (void)score;
#endif
    }

    int DownloadLeaderboardEntries(const char* name, int count, int offset)
    {
#ifdef _WIN32
        if (g_leaderboards.downloadBusy)
            return -1;
        if (g_leaderboards.downloadIoFailure)
            return -2;

        const int index = findLeaderboardIndex(name);
        if (index < 0)
        {
            writeLogLine(g_fileLogger,
                         "!!!ERROR!!!Can't find leaderboard for DownloadLeaderboardEntries(%s)",
                         name ? name : "");
            return -2;
        }

        const std::uint64_t leaderboard = g_leaderboardHandles[static_cast<std::size_t>(index)];
        if (!leaderboard || !g_stats)
        {
            writeLogLine(g_fileLogger,
                         "!!!ERROR!!!Not loaded leaderboard for DownloadLeaderboardEntries(%s)",
                         name ? name : "");
            return -2;
        }

        if (g_downloadEntryCount < 0)
        {
            g_leaderboards.downloadBusy = 1;
            g_leaderboards.downloadIoFailure = 0;
            g_downloadEntryCount = -count;
            const std::uint64_t apiCall = steamVCall<std::uint64_t>(g_stats,
                                                                    112,
                                                                    leaderboard,
                                                                    0,
                                                                    offset,
                                                                    count + offset);
            g_leaderboards.downloadResult.Set(apiCall, &g_leaderboards, &LeaderboardState::OnDownload);
            return -1;
        }

        const int resultCount = g_downloadEntryCount;
        const int safeCount = std::min(resultCount, static_cast<int>(kMaximumDownloadedEntries));
        for (int i = 0; i < safeCount; ++i)
        {
            const LeaderboardEntry& entry = g_downloadedEntries[static_cast<std::size_t>(i)];
            const char* personaName = "";
            if (g_friends)
                personaName = steamVCall<const char*>(g_friends, 28, entry.steamId);
            g_downloadedNames[static_cast<std::size_t>(i)] = convertPersonaNameRetail(personaName);
            g_downloadedScores[static_cast<std::size_t>(i)] = entry.score;
        }
        g_downloadEntryCount = -1;
        return resultCount;
#else
        (void)name;
        (void)count;
        (void)offset;
        return -2;
#endif
    }

    const char* LeaderboardEntryName(int index)
    {
        static const char empty[] = "";
        if (index < 0 || index >= static_cast<int>(kMaximumDownloadedEntries))
            return empty;
        return g_downloadedNames[static_cast<std::size_t>(index)].c_str();
    }

    int LeaderboardEntryScore(int index)
    {
        if (index < 0 || index >= static_cast<int>(kMaximumDownloadedEntries))
            return 0;
        return g_downloadedScores[static_cast<std::size_t>(index)];
    }

    int LastUploadRank() noexcept
    {
#ifdef _WIN32
        return g_leaderboards.lastUploadRank;
#else
        return -1;
#endif
    }
} }
#else
namespace as1 { namespace steam
{
    namespace
    {
        constexpr std::uint32_t kRetailStatsVersion = 2;
        constexpr std::uint32_t kRetailStatsVersionLegacy = 1;
        constexpr std::uint32_t kRetailMaximumRecords = 4096;
        constexpr std::uint32_t kRetailMaximumIdBytes = 4096;
        constexpr std::size_t kRetailMaximumDownloadedEntries = 64;
        constexpr char kRetailStatsMagic[8] = {'A', 'S', '1', 'R', 'S', 'T', 'A', 'T'};

        bool g_retailInitialized = false;
        bool g_retailStatsDirty = false;
        std::string g_retailStatsPath;
        std::map<std::string, std::int32_t> g_retailStats;
        std::map<std::string, bool> g_retailAchievements;
        std::map<std::string, std::int32_t> g_retailLeaderboards;
        std::map<std::string, bool> g_retailLeaderboardNames;
        std::array<std::string, kRetailMaximumDownloadedEntries> g_retailDownloadedNames{};
        std::array<int, kRetailMaximumDownloadedEntries> g_retailDownloadedScores{};
        int g_retailDownloadedCount = 0;
        int g_retailLastUploadRank = 0;

        bool readExact(std::FILE* file, void* data, std::size_t size)
        {
            return file && (size == 0 || std::fread(data, 1, size, file) == size);
        }

        bool writeExact(std::FILE* file, const void* data, std::size_t size)
        {
            return file && (size == 0 || std::fwrite(data, 1, size, file) == size);
        }

        template <typename T>
        bool readValue(std::FILE* file, T& value)
        {
            return readExact(file, &value, sizeof(value));
        }

        template <typename T>
        bool writeValue(std::FILE* file, const T& value)
        {
            return writeExact(file, &value, sizeof(value));
        }

        bool readId(std::FILE* file, std::string& id)
        {
            std::uint32_t length = 0;
            if (!readValue(file, length) || length == 0 || length > kRetailMaximumIdBytes)
                return false;
            id.resize(length);
            return readExact(file, &id[0], length);
        }

        bool writeId(std::FILE* file, const std::string& id)
        {
            if (id.empty() || id.size() > kRetailMaximumIdBytes)
                return false;
            const std::uint32_t length = static_cast<std::uint32_t>(id.size());
            return writeValue(file, length) && writeExact(file, id.data(), length);
        }

        std::string retailSaveDirectory()
        {
            const char* const configured = core::ApplicationSavePath().c_str();
            std::string directory = (configured && *configured) ? configured : "Saves";
            while (!directory.empty() && (directory.back() == '\\' || directory.back() == '/'))
                directory.pop_back();
            if (directory.empty())
                directory = "Saves";
#ifdef _WIN32
            // This is the game's normal root save directory, not a new per-user folder.
            (void)::CreateDirectoryA(directory.c_str(), nullptr);
#endif
            return directory;
        }

        std::string retailStatsPath()
        {
            std::string path = retailSaveDirectory();
#ifdef _WIN32
            path += "\\stats.dat";
#else
            path += "/stats.dat";
#endif
            return path;
        }

        void clearRetailDownloadedEntries() noexcept
        {
            for (auto& name : g_retailDownloadedNames)
                name.clear();
            g_retailDownloadedScores.fill(0);
            g_retailDownloadedCount = 0;
        }

        void clearRetailState()
        {
            g_retailStats.clear();
            g_retailAchievements.clear();
            g_retailLeaderboards.clear();
            g_retailLeaderboardNames.clear();
            clearRetailDownloadedEntries();
            g_retailLastUploadRank = 0;
            g_retailStatsDirty = false;
        }

        bool loadRetailStats()
        {
            clearRetailState();
            if (g_retailStatsPath.empty())
                g_retailStatsPath = retailStatsPath();

            std::FILE* const file = std::fopen(g_retailStatsPath.c_str(), "rb");
            if (!file)
                return true; // First launch is a valid empty state.

            char magic[sizeof(kRetailStatsMagic)]{};
            std::uint32_t version = 0;
            std::uint32_t statCount = 0;
            std::uint32_t achievementCount = 0;
            std::uint32_t leaderboardCount = 0;
            bool ok = readExact(file, magic, sizeof(magic)) &&
                      std::memcmp(magic, kRetailStatsMagic, sizeof(magic)) == 0 &&
                      readValue(file, version) &&
                      (version == kRetailStatsVersionLegacy || version == kRetailStatsVersion) &&
                      readValue(file, statCount) && statCount <= kRetailMaximumRecords &&
                      readValue(file, achievementCount) && achievementCount <= kRetailMaximumRecords;

            if (ok && version >= kRetailStatsVersion)
                ok = readValue(file, leaderboardCount) && leaderboardCount <= kRetailMaximumRecords;

            for (std::uint32_t i = 0; ok && i < statCount; ++i)
            {
                std::string id;
                std::int32_t value = 0;
                ok = readId(file, id) && readValue(file, value);
                if (ok)
                    g_retailStats[id] = value;
            }

            for (std::uint32_t i = 0; ok && i < achievementCount; ++i)
            {
                std::string id;
                std::uint8_t unlocked = 0;
                ok = readId(file, id) && readValue(file, unlocked);
                if (ok)
                    g_retailAchievements[id] = unlocked != 0;
            }

            for (std::uint32_t i = 0; ok && i < leaderboardCount; ++i)
            {
                std::string id;
                std::int32_t score = 0;
                ok = readId(file, id) && readValue(file, score);
                if (ok)
                    g_retailLeaderboards[id] = score;
            }

            std::fclose(file);
            if (!ok)
            {
                clearRetailState();
                if (g_fileLogger)
                    writeLogLine(g_fileLogger, "!!!ERROR!!!RETAIL STORE: invalid stats file '%s'", g_retailStatsPath.c_str());
                return false;
            }
            return true;
        }

        bool saveRetailStats()
        {
            if (g_retailStatsPath.empty())
                g_retailStatsPath = retailStatsPath();

            const std::string temporaryPath = g_retailStatsPath + ".tmp";
            std::FILE* const file = std::fopen(temporaryPath.c_str(), "wb");
            if (!file)
            {
                if (g_fileLogger)
                    writeLogLine(g_fileLogger, "!!!ERROR!!!RETAIL STORE: can't write stats '%s'", temporaryPath.c_str());
                return false;
            }

            const std::uint32_t version = kRetailStatsVersion;
            const std::uint32_t statCount = static_cast<std::uint32_t>(g_retailStats.size());
            const std::uint32_t achievementCount = static_cast<std::uint32_t>(g_retailAchievements.size());
            const std::uint32_t leaderboardCount = static_cast<std::uint32_t>(g_retailLeaderboards.size());
            bool ok = statCount <= kRetailMaximumRecords &&
                      achievementCount <= kRetailMaximumRecords &&
                      leaderboardCount <= kRetailMaximumRecords &&
                      writeExact(file, kRetailStatsMagic, sizeof(kRetailStatsMagic)) &&
                      writeValue(file, version) &&
                      writeValue(file, statCount) &&
                      writeValue(file, achievementCount) &&
                      writeValue(file, leaderboardCount);

            for (const auto& pair : g_retailStats)
            {
                if (!ok)
                    break;
                ok = writeId(file, pair.first) && writeValue(file, pair.second);
            }

            for (const auto& pair : g_retailAchievements)
            {
                if (!ok)
                    break;
                const std::uint8_t unlocked = pair.second ? 1u : 0u;
                ok = writeId(file, pair.first) && writeValue(file, unlocked);
            }

            for (const auto& pair : g_retailLeaderboards)
            {
                if (!ok)
                    break;
                ok = writeId(file, pair.first) && writeValue(file, pair.second);
            }

            if (std::fflush(file) != 0)
                ok = false;
            if (std::fclose(file) != 0)
                ok = false;

            if (!ok)
            {
                std::remove(temporaryPath.c_str());
                if (g_fileLogger)
                    writeLogLine(g_fileLogger, "!!!ERROR!!!RETAIL STORE: failed to serialize stats");
                return false;
            }

#ifdef _WIN32
            if (!::MoveFileExA(temporaryPath.c_str(), g_retailStatsPath.c_str(),
                               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            {
                std::remove(temporaryPath.c_str());
                if (g_fileLogger)
                    writeLogLine(g_fileLogger, "!!!ERROR!!!RETAIL STORE: can't replace stats file (%lu)",
                                 static_cast<unsigned long>(::GetLastError()));
                return false;
            }
#else
            std::remove(g_retailStatsPath.c_str());
            if (std::rename(temporaryPath.c_str(), g_retailStatsPath.c_str()) != 0)
            {
                std::remove(temporaryPath.c_str());
                return false;
            }
#endif

            g_retailStatsDirty = false;
            return true;
        }

        void registerRetailLeaderboards(const char* names)
        {
            if (!names)
                names = "";
            const char* segment = names;
            for (const char* cursor = names;; ++cursor)
            {
                if (*cursor != '#' && *cursor != '\0')
                    continue;
                if (cursor != segment)
                    g_retailLeaderboardNames[std::string(segment, static_cast<std::size_t>(cursor - segment))] = true;
                if (*cursor == '\0')
                    break;
                segment = cursor + 1;
            }
        }

        std::string retailPersonaName()
        {
#ifdef _WIN32
            char name[256]{};
            const DWORD size = ::GetEnvironmentVariableA("USERNAME", name, static_cast<DWORD>(sizeof(name)));
            if (size > 0 && size < sizeof(name) && name[0] != '\0')
                return std::string(name, size);
#endif
            return "Player";
        }
    }

    bool Initialize(int appId)
    {
        (void)appId;
        if (g_retailInitialized)
            return true;

        g_retailStatsPath = retailStatsPath();
        const bool loaded = loadRetailStats();
        g_retailInitialized = true;
        if (g_fileLogger)
            writeLogLine(g_fileLogger, "Retail offline store: %s (%s)",
                         loaded ? "ready" : "started with empty state", g_retailStatsPath.c_str());
        return true;
    }

    void Pump() {}

    void Shutdown()
    {
        if (g_retailInitialized && g_retailStatsDirty)
            (void)saveRetailStats();
        g_retailInitialized = false;
    }

    bool Alive() noexcept { return g_retailInitialized; }

    void SetAchievement(const char* id)
    {
        if (!id || !*id)
            return;
        g_retailAchievements[id] = true;
        g_retailStatsDirty = true;
    }

    int GetAchievement(const char* id)
    {
        if (!id || !*id)
            return 0;
        const auto it = g_retailAchievements.find(id);
        return (it != g_retailAchievements.end() && it->second) ? 1 : 0;
    }

    void ClearAchievement(const char* id)
    {
        if (!id || !*id)
            return;
        g_retailAchievements[id] = false;
        g_retailStatsDirty = true;
    }

    void ResetAllStats()
    {
        g_retailStats.clear();
        g_retailAchievements.clear();
        g_retailLeaderboards.clear();
        clearRetailDownloadedEntries();
        g_retailLastUploadRank = 0;
        g_retailStatsDirty = true;
    }

    void SetStat(const char* id, int value)
    {
        if (!id || !*id)
            return;
        g_retailStats[id] = static_cast<std::int32_t>(value);
        g_retailStatsDirty = true;
    }

    int GetStat(const char* id)
    {
        if (!id || !*id)
            return 0;
        const auto it = g_retailStats.find(id);
        return it != g_retailStats.end() ? static_cast<int>(it->second) : 0;
    }

    void SaveStatsIfNeeded()
    {
        if (g_retailInitialized && g_retailStatsDirty)
            (void)saveRetailStats();
    }

    void ActivateStore(int appId) { (void)appId; }

    void InitLeaderboards(const char* names)
    {
        registerRetailLeaderboards(names);
        if (g_fileLogger)
            writeLogLine(g_fileLogger, "Retail offline leaderboards initialized");
    }

    void UpdateLeaderboard(const char* name, int score)
    {
        if (!name || !*name)
        {
            g_retailLastUploadRank = -2;
            return;
        }

        g_retailLeaderboardNames[name] = true;
        // Steam call uses KeepBest (method 1), so the offline backend mirrors it.
        auto it = g_retailLeaderboards.find(name);
        if (it == g_retailLeaderboards.end() || score > it->second)
            g_retailLeaderboards[name] = static_cast<std::int32_t>(score);
        g_retailLastUploadRank = 1;
        g_retailStatsDirty = true;
    }

    int DownloadLeaderboardEntries(const char* name, int count, int offset)
    {
        clearRetailDownloadedEntries();
        if (!name || !*name || count <= 0)
            return -2;

        // Retail has no network ranking. Return the local player as a completed
        // one-entry leaderboard instead of leaving Steam-oriented scripts waiting
        // for an async callback that can never arrive.
        if (offset > 0)
            return 0;

        g_retailLeaderboardNames[name] = true;
        const auto it = g_retailLeaderboards.find(name);
        const int score = it != g_retailLeaderboards.end() ? static_cast<int>(it->second) : 0;
        g_retailDownloadedNames[0] = retailPersonaName();
        g_retailDownloadedScores[0] = score;
        g_retailDownloadedCount = 1;
        return 1;
    }

    const char* LeaderboardEntryName(int index)
    {
        static const char empty[] = "";
        if (index < 0 || index >= g_retailDownloadedCount ||
            index >= static_cast<int>(kRetailMaximumDownloadedEntries))
            return empty;
        return g_retailDownloadedNames[static_cast<std::size_t>(index)].c_str();
    }

    int LeaderboardEntryScore(int index)
    {
        if (index < 0 || index >= g_retailDownloadedCount ||
            index >= static_cast<int>(kRetailMaximumDownloadedEntries))
            return 0;
        return g_retailDownloadedScores[static_cast<std::size_t>(index)];
    }

    int LastUploadRank() noexcept { return g_retailLastUploadRank; }
} }
#endif

