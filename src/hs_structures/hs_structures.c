#include <stdio.h>
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
local Icmdman *cmd;
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
  HashTable structureInfoTable;  
  LinkedList structures;
  bool attached;
} ArenaData;

typedef struct UserData
{
  bool canBuild;
  bool currentlyBuilding;

  unsigned int nStructures;
  unsigned int structureIdMask;

  struct PlayerPosition startedBuildPos;
  ticks_t startedBuildTime;
  ticks_t lastBuiltTime;
} UserData;

local int adkey = -1;
local int pdkey = -1;
local int buildValidCheckInterval = 100;

local helptext_t BUILD_CMD_HELP =
  "Targets: none\n"
  "Arguments: none\n"
  "Builds a structure at your current location.";

local void updatePlayerData(Player *p, ShipHull *hull, bool dbLock)
{
  UserData *pdata = PPDATA(p, pdkey);
  if (dbLock)
    db->lock();

  pdata->canBuild = (items->getPropertySumOnHull(p, hull, CAN_BUILD_PROP, 0) > 0);

  if (dbLock)
    db->unlock();
}

local bool structureIdToKey(int id, size_t n, char *outKey)
{
  int ret = snprintf(outKey, n, "%d", id);
  return ret >= 0 && ret < n;
}

bool registerStructure(Arena *arena, StructureInfo *structure)
{
  if (!structure || !arena)
    return false;

  char key[8] = {0};
  int id = structure->id;
  if (!structureIdToKey(id, 8, key))
    return false;

  StructureInfo *info = amalloc(sizeof(*info));
  memcpy(info, structure, sizeof(*info));

  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  HashReplace(&adata->structureInfoTable, key, info);

  return true;
}

void unregisterStructure(Arena *arena, int id)
{
  char key[8] = {0};
  if (!structureIdToKey(id, 8, key))
    return;

  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  HashRemoveAny(&adata->structureInfoTable, key);
}

local bool isPowerOfTwo(unsigned int x)
{
  return ((x != 0) && ((x & (~x + 1)) == x));
}

typedef struct BuildInfo
{
  StructureInfo *info;
  Player *p;
} BuildInfo;

local int buildCallback(void *info);

local void stopBuildingLoop(BuildInfo *binfo, const char *message)
{
  UserData *pdata = PPDATA(binfo->p, pdkey);

  chat->SendMessage(binfo->p, message);
  pdata->currentlyBuilding = false;
  ml->ClearTimer(buildCallback, binfo);
}

local int buildCallback(void *info)
{
  BuildInfo *binfo = info;
  UserData *pdata = PPDATA(binfo->p, pdkey);
  ++pdata->nStructures;

  // TODO put maximum distance away from build in config
  if (abs(pdata->startedBuildPos.x - binfo->p->position.x) > 5 || 
    abs(pdata->startedBuildPos.y - binfo->p->position.y) > 5)
  {
    stopBuildingLoop(binfo, "Building cancelled: you moved too far from your site!");
    afree(binfo);
    return TRUE;
  }

  if (!((unsigned int) TICK_DIFF(current_ticks(), pdata->startedBuildTime) >= binfo->info->buildTimeTicks))
    return TRUE;

  Structure *structure = binfo->info->createInstance();

  binfo->info->placedCallback(structure, binfo->p);  
  stopBuildingLoop(binfo, "Structure completed!");
  ml->SetTimer(binfo->info->tickCallback, 0, binfo->info->callbackIntervalTicks, structure, structure);

  afree(binfo);
  pdata->lastBuiltTime = current_ticks();

  return TRUE;
}

local void buildCmd(const char *cmd, const char *params, Player *p, const Target *target)
{
  if (target->type != T_ARENA)
    return;

  UserData *pdata = PPDATA(p, pdkey);
  if (!pdata->canBuild)
  {
    chat->SendMessage(p, "You do not have an item that is capable of building a structure.");
    return;
  } else if (!pdata->structureIdMask) {
    chat->SendMessage(p, "You do not have any structures to build.");
    return;
  }

  if (isPowerOfTwo(pdata->structureIdMask)) // Player has one structure
  {
    char key[8] = {0};
    if (!structureIdToKey(pdata->structureIdMask, 8, key))
      return;

    ArenaData *adata = P_ARENA_DATA(p->arena, adkey);
    StructureInfo *info = HashGetOne(&adata->structureInfoTable, key);
    if (!info)
      return;

    pdata->currentlyBuilding = true;
    pdata->startedBuildTime = current_ticks();

    BuildInfo *binfo = amalloc(sizeof(*binfo));
    binfo->p = p;
    binfo->info = info;

    ml->SetTimer(buildCallback, 0, buildValidCheckInterval, binfo, binfo);    
    chat->SendMessage(p, "Started building structure...");
  } // add support for more than one structure...
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
  cmd = mm->GetInterface(I_CMDMAN, ALLARENAS);
}

local bool checkInterfaces()
{
  if (aman && chat && cfg && db && fake && game && items && map && ml && net && pd && cmd)
    return true;
  return false;
}

local void releaseInterfaces()
{
  mm->ReleaseInterface(aman);
  mm->ReleaseInterface(chat);
  mm->ReleaseInterface(cmd);
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
    pdkey = pd->AllocatePlayerData(sizeof(struct UserData));

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
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    if (adata->attached)
      return MM_FAIL;
    // readConfig(arena->cfg, adata);

    HashInit(&adata->structureInfoTable);
    LLInit(&adata->structures);

    cmd->AddCommand("build", buildCmd, arena, BUILD_CMD_HELP);
    adata->attached = true;
    return MM_OK;
  }
  else if (action == MM_DETACH)
  {
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    if (!adata->attached)
      return MM_FAIL;
  
    HashDeinit(&adata->structureInfoTable);
    LLEmpty(&adata->structures);
    adata->attached = false;
    return MM_OK;
  }

  return MM_FAIL;
}