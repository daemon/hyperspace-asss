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

local void destroyTripwire(struct Structure *structure)
{
  afree(structure->extraData);
  afree(structure);
}

local void tripwirePlacedCallback(Structure *structure, Player *owner)
{
  structure->owner = owner;
  // TODO: Make configurable
  structure->fakePlayer = fake->CreateFakePlayer("<tripwire>", owner->arena, SHIP_WARBIRD, owner->p_freq);
  
  struct C2SPosition *pos = structure->extraData;
  pos->x = owner->position.x;
  pos->y = owner->position.y;
}

local void tripwireDestroyedCallback(Structure *tripwire, Player *killer)
{
  struct KillPacket objPacket;
  objPacket.type = S2C_KILL;
  objPacket.killer = killer->pid;
  objPacket.killed = tripwire->fakePlayer->pid;
  objPacket.bounty = 10;
  objPacket.flags = 0;

  net->SendToArena(killer->arena, NULL, (byte *) &objPacket, sizeof(objPacket), NET_RELIABLE);
  fake->EndFaked(tripwire->fakePlayer);
}

local int tripwireTickCallback(void *structure)
{
  Structure *tripwire = structure;
  Player *fakePlayer = tripwire->fakePlayer;

  ((struct C2SPosition *) tripwire->extraData)->time = current_ticks();

  game->FakePosition(fakePlayer, tripwire->extraData, sizeof(struct C2SPosition));
  return TRUE;
}

local bool canBuildTripwire(Player *builder)
{
  Region *notCenter = map->FindRegionByName(builder->arena, "sector0");
  Region *notId = map->FindRegionByName(builder->arena, "interdimensional");

  bool inRightRegion = !map->Contains(notCenter, builder->position.x >> 4, builder->position.y >> 4) &&
    !map->Contains(notId, builder->position.x >> 4, builder->position.y >> 4);

  return inRightRegion;
}

// TODO: Make configurable
local void initWeapon(struct C2SPosition *packet)
{
  packet->type = C2S_POSITION;
  packet->bounty = 0;
  packet->energy = 500;
  packet->weapon.type = W_BOUNCEBULLET;
  packet->weapon.level = 2;
}

local Structure *createTripwire(void);

local void initTripwireInfo(StructureInfo *info)
{
  // Make stuff configurable
  info->id = 1;
  info->callbackIntervalTicks = 100;
  info->buildTimeTicks    = 500;
  info->canBuild          = canBuildTripwire;
  info->createInstance    = createTripwire;
  info->destroyInstance   = destroyTripwire;
  info->tickCallback      = tripwireTickCallback;
  info->placedCallback    = tripwirePlacedCallback;
  info->destroyedCallback = tripwireDestroyedCallback;
}

local Structure *createTripwire(void)
{
  Structure *structure = amalloc(sizeof(*structure));
  initTripwireInfo(&structure->info);
  structure->extraData = amalloc(sizeof(struct C2SPosition));
  initWeapon(structure->extraData);

  return structure;
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

EXPORT int MM_hs_tripwire(int action, Imodman *mm_, Arena *arena)
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

    return MM_OK;
  }
  else if (action == MM_UNLOAD)
  {
    releaseInterfaces();
    return MM_OK;
  }
  else if (action == MM_ATTACH)
  {
    // TODO: double attach checking...
    StructureInfo tripwireInfo;
    initTripwireInfo(&tripwireInfo);
    if (!registerStructure(arena, &tripwireInfo))
      return MM_FAIL;
    return MM_OK;
  }
  else if (action == MM_DETACH)
  {
    // TODO configurable
    unregisterStructure(arena, 1);
    return MM_OK;
  }

  return MM_FAIL;
}