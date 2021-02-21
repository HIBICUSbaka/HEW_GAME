#include "DataSyncer.h"
#include "DatabaseManager.h"
#include "LogsOutput.h"
#include <vector>
#include <algorithm>

MYSQL g_DatabaseHandle;
MYSQL* gp_ConnectHandle = NULL;
MYSQL_RES* gp_SqlResult = NULL;
static HANDLE g_HandleUpdateRank = NULL;
static HANDLE g_HandleUpdateBuild = NULL;
static HANDLE g_HandleGetRankAndBuild = NULL;
int g_StageRankNum[5] = { 0,0,0,0,0 };
int g_MostUsedBuildsID[3] = { 0,0,0 };
DATA_UPDATE_RANK g_UpdateRankArgument;
DATA_UPDATE_BUILD g_UpdateBuildArgument;
int g_CanUseDatabase = 0;

void ConnectToHewDatabase()
{
    if (gp_ConnectHandle != NULL)
    {
        CloseConnectHandle(gp_ConnectHandle);
    }
    InitDatabaseHandle(&g_DatabaseHandle);
    gp_ConnectHandle = ConnectToDatabase(
        &g_DatabaseHandle,
        "106.53.96.176",
        "player", "player",
        "hew_game"
    );
}

void CloseConnectToHewDatabase()
{
    CloseConnectHandle(gp_ConnectHandle);
    InitDatabaseHandle(&g_DatabaseHandle);
    gp_ConnectHandle = NULL;
}

void TestHewDatabaseConnect()
{
    ConnectToHewDatabase();
    if (gp_ConnectHandle!=NULL)
    {
        FreeResultPointer(gp_SqlResult);
        gp_SqlResult = RunQueryAndGetResult(&g_DatabaseHandle,
            "select * from stage_rank");
        DebugLogI1("row's count : ", GetResultRowCount(gp_SqlResult));
        FreeResultPointer(gp_SqlResult);
        CloseConnectToHewDatabase();
        g_CanUseDatabase = 1;

        int a = 3;
        GetStageRankAndBuildCount(&a);
    }
    else
    {
        g_CanUseDatabase = 0;
    }
}

void InitDataSyncerCountArray()
{
    for (int i = 0; i < 5; i++)
    {
        g_StageRankNum[i] = 0;
    }
    for (int i = 0; i < 3; i++)
    {
        g_MostUsedBuildsID[i] = 0;
    }
}

void UpdateStageRankMT(DATA_UPDATE_RANK* data)
{
    int stage_rank_id = 0;
    stage_rank_id = data->StageID * 10 + data->Rank;

    ConnectToHewDatabase();
    char updateRank[100];
    sprintf_s(updateRank, sizeof(updateRank),
        "update hew_game.stage_rank set COUNT=COUNT+1 where STAGE_RANK_ID=%d"
        , stage_rank_id);
    int status = RunQueryRequest(&g_DatabaseHandle, updateRank);
    if (status)
    {
        ErrorLog("cannot update stage rank");
    }
    CloseConnectToHewDatabase();
}

void UpdateStageBuildTypeMT(DATA_UPDATE_BUILD* data)
{
    int stage_buildtype_id = 0;
    char updateRank[100];
    ConnectToHewDatabase();
    for (int i = 0; i < 8; i++)
    {
        stage_buildtype_id = data->StageID * 10 + i + 1;
        if (data->BuildTypeCount[i])
        {
            sprintf_s(updateRank, sizeof(updateRank),
                "update hew_game.stage_build set COUNT=COUNT+%d where STAGE_BUILDTYPE_ID=%d",
                data->BuildTypeCount[i],
                stage_buildtype_id);
            int status = RunQueryRequest(&g_DatabaseHandle, updateRank);
            if (status)
            {
                ErrorLog("cannot update stage build");
            }
        }
    }
    CloseConnectToHewDatabase();
}

void GetStageRankAndBuildCount(int* stageID)
{
    InitDataSyncerCountArray();
    ConnectToHewDatabase();

    gp_SqlResult = RunQueryAndGetResult(&g_DatabaseHandle,
        "select * from stage_rank");
    MYSQL_ROW row;

    while ((row = mysql_fetch_row(gp_SqlResult)) != NULL)
    {
        if (row[0][0] == (*stageID + 48))
        {
            int len = strlen(row[1]);
            int value = 0;

            for (int i = len; i > 0; i--)
            {
                int base = 1;
                for (int j = 0; j < (i - 1); j++)
                {
                    base *= 10;
                }
                value += (row[1][len - i] - 48) * base;
            }

            g_StageRankNum[row[0][1] - 49] = value;
        }
    }
    FreeResultPointer(gp_SqlResult);

    gp_SqlResult = RunQueryAndGetResult(&g_DatabaseHandle,
        "select * from stage_build");
    int buildCount[8] = { 0,0,0,0,0,0,0,0 };
    while ((row = mysql_fetch_row(gp_SqlResult)) != NULL)
    {
        if (row[0][0] == (*stageID + 48))
        {
            int len = strlen(row[1]);
            int value = 0;

            for (int i = len; i > 0; i--)
            {
                int base = 1;
                for (int j = 0; j < (i - 1); j++)
                {
                    base *= 10;
                }
                value += (row[1][len - i] - 48) * base;
            }

            buildCount[row[0][1] - 49] = value;
        }
    }
    std::vector<int> buildCountV(buildCount, buildCount + 8);
    std::sort(buildCountV.begin(), buildCountV.end());
    for (int i = 0; i < 3; i++)
    {
        int value = buildCountV.back();
        buildCountV.pop_back();
        for (int j = 0; j < 8; j++)
        {
            if (buildCount[j] == value)
            {
                if (i == 0)
                {
                    g_MostUsedBuildsID[i] = j;
                    break;
                }
                else if (i == 1)
                {
                    if (j != g_MostUsedBuildsID[0])
                    {
                        g_MostUsedBuildsID[i] = j;
                        break;
                    }
                }
                else
                {
                    if (j != g_MostUsedBuildsID[0] &&
                        j != g_MostUsedBuildsID[1])
                    {
                        g_MostUsedBuildsID[i] = j;
                        break;
                    }
                }

            }
        }
    }
    FreeResultPointer(gp_SqlResult);

    CloseConnectToHewDatabase();
}

void CreateUpdateStageRankThread()
{
    DWORD dw;
    g_HandleUpdateRank = CreateThread(
        NULL, 0,
        (LPTHREAD_START_ROUTINE)UpdateStageRankMT,
        &g_UpdateRankArgument,
        0, &dw
    );
}

void CreateUpdateStageBuildThread()
{
    DWORD dw;
    g_HandleUpdateRank = CreateThread(
        NULL, 0,
        (LPTHREAD_START_ROUTINE)UpdateStageBuildTypeMT,
        &g_UpdateBuildArgument,
        0, &dw
    );
}

void CreateGetStageRankAndBuildThread(int stageID)
{
    DWORD dw;
    g_HandleUpdateRank = CreateThread(
        NULL, 0,
        (LPTHREAD_START_ROUTINE)GetStageRankAndBuildCount,
        &stageID,
        0, &dw
    );
}

void CloseUpdateStageRankThread()
{
    if (gp_ConnectHandle != NULL)
    {
        CloseConnectHandle(gp_ConnectHandle);
    }

    CloseHandle(g_HandleUpdateRank);
}

void CloseUpdateStageBuildThread()
{
    if (gp_ConnectHandle != NULL)
    {
        CloseConnectHandle(gp_ConnectHandle);
    }

    CloseHandle(g_HandleUpdateBuild);
}

void CloseGetStageRankAndBuildThread()
{
    if (gp_ConnectHandle != NULL)
    {
        CloseConnectHandle(gp_ConnectHandle);
    }

    CloseHandle(g_HandleGetRankAndBuild);
}

DATA_UPDATE_RANK* GetUpdateRankAddr()
{
    return &g_UpdateRankArgument;
}

DATA_UPDATE_BUILD* GetUpdateBuildAddr()
{
    return &g_UpdateBuildArgument;
}

int CanUseDataBase()
{
    return g_CanUseDatabase;
}
