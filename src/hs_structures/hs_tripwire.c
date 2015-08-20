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
local Istructman *sm;
local Ihscoredatabase *db;

local void destroyTripwire(struct Structure *structure)
{
  afree(structure->extraData);
  afree(structure);
}

local inline int closest90RotOf(int shipRot)
{
  return round(shipRot / 10.0) * 90;
}

local void tripwirePlacedCallback(Structure *structure, Player *owner, struct PlayerPosition *buildPos)
{  
  struct C2SPosition *pos = structure->extraData;
  pos->x = buildPos->x;
  pos->y = buildPos->y;
  pos->rotation = (closest90RotOf(buildPos->rotation) / 90) * 10;

  structure->fakePlayer->position.energy = 500;
  structure->fakePlayer->position.x = pos->x;
  structure->fakePlayer->position.y = pos->y;
  structure->fakePlayer->position.rotation = pos->rotation;
}

local void tripwireDestroyedCallback(Structure *tripwire, Player *killer)
{
  
}

local int tripwireTickCallback(Structure *tripwire)
{
  Player *fakePlayer = tripwire->fakePlayer;

  struct C2SPosition *ppk = (struct C2SPosition *) tripwire->extraData;
  ppk->time = current_ticks();
  ppk->energy = fakePlayer->position.energy;

  game->FakePosition(fakePlayer, tripwire->extraData, sizeof(struct C2SPosition));
  return TRUE;
}

local void tripwireDamagedCallback(Structure *tripwire, Player *shooter, int damage)
{
  tripwire->fakePlayer->position.energy -= damage;
  if (tripwire->fakePlayer->position.energy < 0)
    tripwire->fakePlayer->position.energy = 0;
}

local bool canBuildTripwire(Player *builder)
{
  Region *center = map->FindRegionByName(builder->arena, "sector0");
  Region *interdim = map->FindRegionByName(builder->arena, "interdimensional");

  int mapNormX = builder->position.x >> 4;
  int mapNormY = builder->position.y >> 4;

  bool inRightRegion = !map->Contains(center, mapNormX, mapNormY) &&
    !map->Contains(interdim, mapNormX, mapNormY);

  bool adjWall = false;
  for (int i = -3; i <= 3; ++i)
    for (int j = -3; j <= 3; ++j)
    {
      if (!i && !j)
        continue;

      int tile = map->GetTile(builder->arena, mapNormX + i, mapNormY + j);
      if (tile >= TILE_START && tile <= TILE_END)
      {
        adjWall = true;
        break;
      }
    }
// FINISH
  bool validLen = false;
  bool validLenAnterior = false;
  bool validLenPosterior = false;
  int dir = closest90RotOf(builder->position.rotation);
  for (int tile1 = 0, tile2 = 0, i = 0; i < 12; ++i)
  {
    switch (dir)
    {
    case 0:
      tile1 = map->GetTile(builder->arena, mapNormX, mapNormY - i);
      tile2 = map->GetTile(builder->arena, mapNormX, mapNormY + i);
      break;
    case 90:
      tile1 = map->GetTile(builder->arena, mapNormX + i, mapNormY);
      tile2 = map->GetTile(builder->arena, mapNormX - i, mapNormY);
      break;
    case 180:
      tile1 = map->GetTile(builder->arena, mapNormX, mapNormY + i);
      tile2 = map->GetTile(builder->arena, mapNormX, mapNormY - i);
      break;
    case 270:
      tile1 = map->GetTile(builder->arena, mapNormX - i, mapNormY);
      tile2 = map->GetTile(builder->arena, mapNormX + i, mapNormY);
      break;
    }
    if (tile1 >= TILE_START && tile1 <= TILE_END)
      validLenAnterior = true;
    if (tile2 >= TILE_START && tile2 <= TILE_END)
      validLenPosterior = true;    
    if (validLen = validLenAnterior && validLenPosterior)
      break;
  }

  if (!adjWall && inRightRegion)
    chat->SendMessage(builder, "Tripwire must be next to a wall.");
  else if (!validLen && inRightRegion)
    chat->SendMessage(builder, "The opposite wall is too far away.");

  return inRightRegion && adjWall && validLen;
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
  info->callbackIntervalTicks = 50;
  info->buildTimeTicks    = 500;
  info->fakePlayerName    = "<tripwire>";
  info->shipNo            = SHIP_WARBIRD;
  info->canBuild          = canBuildTripwire;
  info->createInstance    = createTripwire;
  info->destroyInstance   = destroyTripwire;
  info->tickCallback      = tripwireTickCallback;
  info->placedCallback    = tripwirePlacedCallback;
  info->destroyedCallback = tripwireDestroyedCallback;
  info->damagedCallback   = tripwireDamagedCallback;
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
  sm = mm->GetInterface(I_STRUCTMAN, ALLARENAS);
  pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
}

local bool checkInterfaces()
{
  if (aman && chat && cfg && db && fake && game && items && map && ml && net && pd && sm)
    return true;
  return false;
}

local void releaseInterfaces()
{
  mm->ReleaseInterface(sm);
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
    if (!sm->RegisterStructure(arena, &tripwireInfo))
      return MM_FAIL;
    return MM_OK;
  }
  else if (action == MM_DETACH)
  {
    // TODO configurable
    sm->UnregisterStructure(arena, 1);
    return MM_OK;
  }

  return MM_FAIL;
}