#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hs_structures.h"
#include "packets/kill.h"

local Imodman *mm;
local Ilogman *lm;
local Ifake *fake;
local Iarenaman *aman;
local Igame *game;
local Icmdman *cmdman;
local Ichat *chat;
local Iconfig *cfg;
local Imainloop *ml;
local Iplayerdata *pd;
local Ihscoreitems *items;
local Inet *net;
local Imapdata *map;
local Ihscoredatabase *db;

typedef struct ArenaData
{
  LinkedList structures;
} ArenaData;

typedef struct PlayerData
{
  bool canBuild;
  ticks_t lastBuilt;
} PlayerData;

int adkey, pdkey;

local helptext_t BUILD_CMD_HELP =
  "Targets: none\n"
  "Arguments: none\n"
  "Builds a structure at your current location.";

// Item properties
local const char ID_PROP = "structureidmask";
local const char CAN_BUILD_PROP = "canbuild";
local const char BUILD_DELAY_PROP = "builddelay";

local void buildCmd(const char *cmd, const char *params, Player *p, const Target *target)
{
}

local void getInterfaces()
{
  aman = mm->GetInterface(I_ARENAMAN, ALLARENAS);
  chat = mm->GetInterface(I_CHAT, ALLARENAS);
  cfg = mm->GetInterface(I_CONFIG, ALLARENAS);
  db = mm->GetInterface(I_HSCORE_DATABASE, ALLARENAS);
  fake = mm->GetInterface(I_FAKE, ALLARENAS);
  game = mm->GetInterface(I_GAME, ALLARENAS);
  items = mm->GetInterface(I_HSCORE_ITEMS, ALLARENAS);
  map = mm->GetInterface(I_MAPDATA, ALLARENAS);
  ml = mm->GetInterface(I_MAINLOOP, ALLARENAS);
  net = mm->GetInterface(I_NET, ALLARENAS);
  pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
}

local bool checkInterfaces()
{
  if (aman && chat && cfg && db && fake && game && items && map && ml && net && pd)
    return true;
  return false;
}

local void releaseInterfaces()
{
  mm->ReleaseInterface(aman);
  mm->ReleaseInterface(chat);
  mm->ReleaseInterface(cfg);
  mm->ReleaseInterface(db);
  mm->ReleaseInterface(fake);
  mm->ReleaseInterface(game);
  mm->ReleaseInterface(items);
  mm->ReleaseInterface(map);
  mm->ReleaseInterface(ml);
  mm->ReleaseInterface(net);
  mm->ReleaseInterface(pd);
}

EXPORT int MM_hs_structures(int action, Imodman *mm_, Arena *arena)
{
  if (action == MM_LOAD)
  {
    mm = mm_;

    getInterfaces();
    if (!checkInterfaces())
    {
      releaseInterfaces();
      return MM_FAIL;
    }

    adkey = aman->AllocateArenaData(sizeof(struct ArenaData));
    pdkey = pd->AllocatePlayerData(sizeof(struct PlayerData));

    if (adkey == -1 || pdkey == -1) //Memory check
    {
      if (adkey  != -1) //free data if it was allocated
        aman->FreeArenaData(adkey);

      if (pdkey != -1) //free data if it was allocated
        pd->FreePlayerData (pdkey);

      releaseInterfaces();
      return MM_FAIL;
    }

    return MM_OK;
  }
  else if (action == MM_UNLOAD)
  {
    aman->FreeArenaData(adkey);
    pd->FreePlayerData(pdkey);

    releaseInterfaces();

    return MM_OK;
  }
  else if (action == MM_ATTACH)
  {
    ArenaData *adata = getArenaData(arena);
    readConfig(arena->cfg, adata);

    cmd->AddCommand("build", buildCmd, arena, BUILD_CMD_HELP);

    if (adata->Config.useTurretLimit)
    {
      mm->RegAdviser(&buysellAdviser, arena);
      mm->RegAdviser(&itemsAdviser, arena);
    }

    if (adata->Config.killsToOwner)
    {
      mm->RegAdviser(&killAdviser, arena);
    }

    //Initialize arena-wide turret linkedlist
    LLInit(&adata->turrets);

    //Initialize all turret hashtables
    PlayerData *pdata;
    Player *p;
    Link *link;

    pd->Lock();
    FOR_EACH_PLAYER_IN_ARENA(p, arena)
    {
      if (!IS_HUMAN(p)) continue;

      pdata = getPlayerData(p);
      pdata->turrets = HashAlloc();
    }
    pd->Unlock();

    mm->RegCallback(CB_SHIPSET_CHANGED, shipsetChangedCB, arena);
    mm->RegCallback(CB_PLAYERACTION, playerActionCB, arena);
    mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCB, arena);
    mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCB, arena);
    mm->RegCallback(CB_PLAYERDAMAGE, playerDamageCB, arena);
    mm->RegCallback(CB_KILL, killCB, arena);
    mm->RegCallback(CB_PPK, ppkCB, arena);

    ml->SetTimer(turretUpdateTimer, 1, 1, arena, arena);

    adata->attached = true;

    unlock(adata);

    return MM_OK;
  }
  else if (action == MM_DETACH)
  {
    return MM_OK;
  }

  return MM_FAIL;
}