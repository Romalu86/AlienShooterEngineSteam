#pragma once

namespace as1 { namespace steam
{
    constexpr int AppId = 33100;

    bool Initialize(int appId = AppId);
    void Pump();
    void Shutdown();
    bool Alive() noexcept;

    void SetAchievement(const char* id);
    int GetAchievement(const char* id);
    void ClearAchievement(const char* id);
    void ResetAllStats();
    void SetStat(const char* id, int value);
    int GetStat(const char* id);
    void SaveStatsIfNeeded();
    void ActivateStore(int appId);

    void InitLeaderboards(const char* names);
    void UpdateLeaderboard(const char* name, int score);
    int DownloadLeaderboardEntries(const char* name, int count, int offset);
    const char* LeaderboardEntryName(int index);
    int LeaderboardEntryScore(int index);
    int LastUploadRank() noexcept;
} }
