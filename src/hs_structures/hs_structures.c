#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hs_structures.h"
#include "hs_weptrack/hs_weptrack.h"
#include "packets/kill.h"
#include "watchdamage.h"

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
local Iweptrack *iwt;

typedef struct ArenaData
{
  pthread_mutex_t arenaMtx;
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

// Prototypes
local void killCb(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green);
local void playerDamageCb(Arena *arena, Player *p, struct S2CWatchDamage *s2cdamage, int count);
local void updatePlayerData(Player *p, ShipHull *hull, bool dbLock);
local void arenaActionCb(Arena *arena, int action);
local void itemsChangedCb(Player *p, ShipHull *hull);
local void shipFreqChangeCb(Player *p, int newship, int oldship, int newfreq, int oldfreq);
local bool structureIdToKey(int id, size_t n, char *outKey);
local bool isPowerOfTwo(unsigned int x);
local int buildCallback(void *info);
local void buildCmd(const char *cmd, const char *params, Player *p, const Target *target);
local void getInterfaces(void);
local bool checkInterfaces(void);
local void releaseInterfaces(void);
local int structureTick(void *data);
local void trackWeaponsCb(const TrackEvent *event);

local helptext_t BUILD_CMD_HELP =
  "Targets: none\n"
  "Arguments: none\n"
  "Builds a structure at your current location.";

local void killCb(Arena *arena, Player *killer, Player *killed, int bounty, int flags, int *pts, int *green)
{
  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  Structure *structure;
  Link *link;

  if (!LLCount(&adata->structures))
    return;

  pthread_mutex_lock(&adata->arenaMtx);
  FOR_EACH(&adata->structures, structure, link)
  {
    if (structure->fakePlayer->pid != killed->pid) 
      continue;

    structure->info.destroyedCallback(structure, killer);
    structure->info.destroyInstance(structure);
    LLRemove(&adata->structures, structure);
    break;
  }  
  pthread_mutex_unlock(&adata->arenaMtx);
}

local void playerDamageCb(Arena *arena, Player *p, struct S2CWatchDamage *s2cdamage, int count)
{
  
}

local void updatePlayerData(Player *p, ShipHull *hull, bool dbLock)
{
  UserData *pdata = PPDATA(p, pdkey);
  if (dbLock)
    db->lock();

  pdata->canBuild = (items->getPropertySumOnHull(p, hull, CAN_BUILD_PROP, 0) > 0);
  pdata->structureIdMask = items->getPropertySumOnHull(p, hull, ID_PROP, 0);

  if (dbLock)
    db->unlock();
}

local void arenaActionCb(Arena *arena, int action)
{

}

// TODO: add player enter and leave arena callback

local void itemsChangedCb(Player *p, ShipHull *hull)
{
  if (!IS_STANDARD(p) || !hull)
    return;
  if (db->getPlayerCurrentHull(p) == hull)
    updatePlayerData(p, hull, false);
}

local void shipFreqChangeCb(Player *p, int newship, int oldship, int newfreq, int oldfreq)
{
  if (!IS_STANDARD(p))
    return;
  updatePlayerData(p, db->getPlayerShipHull(p, newship), true);
}

local bool structureIdToKey(int id, size_t n, char *outKey)
{
  int ret = snprintf(outKey, n, "%d", id);
  return ret >= 0 && ret < n;
}

local bool registerStructure(Arena *arena, StructureInfo *structure)
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
  pthread_mutex_lock(&adata->arenaMtx);
  HashReplace(&adata->structureInfoTable, key, info);
  pthread_mutex_unlock(&adata->arenaMtx);

  return true;
}

local void unregisterStructure(Arena *arena, int id)
{
  char key[8] = {0};
  if (!structureIdToKey(id, 8, key))
    return;

  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  pthread_mutex_lock(&adata->arenaMtx);
  HashRemoveAny(&adata->structureInfoTable, key);
  pthread_mutex_unlock(&adata->arenaMtx);
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

local void stopBuildingLoop(BuildInfo *binfo, const char *message)
{
  UserData *pdata = PPDATA(binfo->p, pdkey);

  chat->SendMessage(binfo->p, message);
  pdata->currentlyBuilding = false;
  ml->ClearTimer(buildCallback, binfo);
}

local int structureTick(void *data)
{
  Structure *structure = data;
  pthread_mutex_lock(&structure->mtx);
  int rc = structure->info.tickCallback(structure);
  pthread_mutex_unlock(&structure->mtx);

  return rc;
}

// fix: use a key for each event
local void trackWeaponsCb(const TrackEvent *event)
{
  if (event->type != PLAYER_COLLISION_EVENT && event->type != WALL_COLLISION_EVENT)
    return;

  ArenaData *adata = P_ARENA_DATA(event->weapon->shooter->arena, adkey);
  if (!LLCount(&adata->structures))
    return;

  Structure *structure;
  Link *link;

  pthread_mutex_lock(&adata->arenaMtx);
  FOR_EACH(&adata->structures, structure, link)
  {
    Player *fp = structure->fakePlayer;
    if (structure->wepTrackKey != event->trackKey || 
      (event->type == PLAYER_COLLISION_EVENT && fp != event->data.collidedPlayer))
      continue;

    structure->info.damagedCallback(structure, event->weapon->shooter, 10);

    struct C2SPosition dmgPpk = {0};
    dmgPpk.type = C2S_POSITION;
    dmgPpk.bounty = 0;
    dmgPpk.energy = fp->position.energy;
    dmgPpk.weapon.type = W_NULL;
    dmgPpk.x = fp->position.x;
    dmgPpk.y = fp->position.y;

    game->FakePosition(fp, &dmgPpk, sizeof(struct C2SPosition));
    if (fp->position.energy == 0)
    {
      struct KillPacket objPacket;
      objPacket.type = S2C_KILL;
      objPacket.killer = event->weapon->shooter->pid;
      objPacket.killed = structure->fakePlayer->pid;
      objPacket.bounty = 0;
      objPacket.flags = 0;

      net->SendToArena(event->weapon->shooter->arena, NULL, (byte *) &objPacket, sizeof(objPacket), NET_RELIABLE);
      iwt->UnregWepTracking(structure->wepTrackKey);
      fake->EndFaked(structure->fakePlayer);
      ml->ClearTimer(structureTick, structure);

      pthread_mutex_lock(&structure->mtx);
      structure->info.destroyedCallback(structure, event->weapon->shooter);
      pthread_mutex_unlock(&structure->mtx);
      pthread_mutex_destroy(&structure->mtx);

      LLRemove(&adata->structures, structure);
    }
  }
  pthread_mutex_unlock(&adata->arenaMtx);
  
}

local int buildCallback(void *info)
{
  BuildInfo *binfo = info;
  UserData *pdata = PPDATA(binfo->p, pdkey);
  ++pdata->nStructures;

  // TODO put maximum distance away from build in config
  if (abs(pdata->startedBuildPos.x - binfo->p->position.x) > 32 || 
    abs(pdata->startedBuildPos.y - binfo->p->position.y) > 32)
  {
    stopBuildingLoop(binfo, "Building cancelled: you moved too far from your site!");
    --pdata->nStructures;
    afree(binfo);
    return TRUE;
  }

  if (!((unsigned int) TICK_DIFF(current_ticks(), pdata->startedBuildTime) >= binfo->info->buildTimeTicks))
    return TRUE;

  Structure *structure = binfo->info->createInstance();
  pthread_mutex_init(&structure->mtx, NULL);

  ArenaData *adata = P_ARENA_DATA(binfo->p->arena, adkey);
  pthread_mutex_lock(&adata->arenaMtx);
  LLAdd(&adata->structures, structure);
  pthread_mutex_unlock(&adata->arenaMtx);

  structure->owner = binfo->p;
  structure->fakePlayer = fake->CreateFakePlayer(binfo->info->fakePlayerName, 
    binfo->p->arena, binfo->info->shipNo, binfo->p->p_freq);

  int x = pdata->startedBuildPos.x;
  int y = pdata->startedBuildPos.y;
  binfo->info->placedCallback(structure, binfo->p, &pdata->startedBuildPos);

  stopBuildingLoop(binfo, "Structure completed!");

  WepTrackInfo wepInfo = {
    {x - 1024, y - 1024, x + 1024, y + 1024},
    trackWeaponsCb, TRACK_BULLET | TRACK_BOMB
  };

  int key = iwt->RegWepTracking(binfo->p->arena, wepInfo);
  structure->wepTrackKey = key;

  WepTrackRect bombTrackRect = {
    x - 384, y - 384, x + 384, y + 384
  };

  if (structure->fakePlayer)
  {
    //iwt->AddPlayerCollision(structure->fakePlayer, key);
    //iwt->AddWallCollision(bombTrackRect, key);
    iwt->AddAnyPlayerCollision(bombTrackRect, key);
  }

  ml->SetTimer(structureTick, 0, binfo->info->callbackIntervalTicks, structure, structure);

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
  } else if (pdata->currentlyBuilding) {
    chat->SendMessage(p, "You cannot build multiple structures at once.");
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
    else if (!info->canBuild(p))
    {
      chat->SendMessage(p, "You cannot build this structure here.");
      return;
    }

    pdata->currentlyBuilding = true;
    pdata->startedBuildTime = current_ticks();
    pdata->startedBuildPos = p->position;

    BuildInfo *binfo = amalloc(sizeof(*binfo));
    binfo->p = p;
    binfo->info = info;

    ml->SetTimer(buildCallback, 0, buildValidCheckInterval, binfo, binfo);    
    chat->SendMessage(p, "Started building structure...");
  } else {
    chat->SendMessage(p, "Error: you either have multiple structures on a ship or configuration is bad.");
  } // add support for ship having more than one structure...
}

local void getInterfaces(void)
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
  lm = mm->GetInterface(I_LOGMAN, ALLARENAS);
  iwt = mm->GetInterface(I_WEPTRACK, ALLARENAS);
}

local bool checkInterfaces(void)
{
  if (aman && chat && cfg && db && fake && game && items && map && ml && net && pd && cmd && iwt && lm)
    return true;
  return false;
}

local void releaseInterfaces(void)
{
  mm->ReleaseInterface(lm);
  mm->ReleaseInterface(iwt);
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

local Istructman smInt = {
  INTERFACE_HEAD_INIT(I_STRUCTMAN, "structman")
  registerStructure, unregisterStructure
};

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

    mm->RegInterface(&smInt, ALLARENAS);
    return MM_OK;
  }
  else if (action == MM_UNLOAD)
  {
    aman->FreeArenaData(adkey);
    pd->FreePlayerData(pdkey);

    releaseInterfaces();    
    if (mm->UnregInterface(&smInt, ALLARENAS))
      return MM_FAIL;

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

    mm->RegCallback(CB_ARENAACTION, arenaActionCb, arena);
    mm->RegCallback(CB_ITEMS_CHANGED, itemsChangedCb, arena);
    mm->RegCallback(CB_SHIPFREQCHANGE, shipFreqChangeCb, arena);
    // mm->RegCallback(CB_PPK, ppkCb, arena);
    mm->RegCallback(CB_KILL, killCb, arena);
    mm->RegCallback(CB_PLAYERDAMAGE, playerDamageCb, arena);

    cmd->AddCommand("build", buildCmd, arena, BUILD_CMD_HELP);
    adata->attached = true;

    pthread_mutex_init(&adata->arenaMtx, NULL);
    return MM_OK;
  }
  else if (action == MM_DETACH)
  {
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    if (!adata->attached)
      return MM_FAIL;

    // mm->UnregCallback(CB_PPK, ppkCb, arena);
    mm->UnregCallback(CB_PLAYERDAMAGE, playerDamageCb, arena);
    mm->UnregCallback(CB_ARENAACTION, arenaActionCb, arena);
    mm->UnregCallback(CB_ITEMS_CHANGED, itemsChangedCb, arena);
    mm->UnregCallback(CB_SHIPFREQCHANGE, shipFreqChangeCb, arena);
    mm->UnregCallback(CB_KILL, killCb, arena);
  
    HashDeinit(&adata->structureInfoTable);
    LLEmpty(&adata->structures);
    adata->attached = false;

    pthread_mutex_destroy(&adata->arenaMtx);
    return MM_OK;
  }

  return MM_FAIL;
}