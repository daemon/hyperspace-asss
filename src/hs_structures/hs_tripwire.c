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

typedef struct StructureInfo
{
  int id;
  unsigned int callbackIntervalTicks;
  unsigned int buildTimeTicks;
  struct Structure *(*createInstance)(void);
  void (*destroyInstance)(struct Structure *structure);
  int (*tickCallback)(void *structure);
  void (*placedCallback)(struct Structure *structure, Player *builder);
  void (*destroyedCallback)(struct Structure *structure, Player *killer);
} StructureInfo;

void destroyTripwire(struct Structure *structure)
{
  afree(structure->extraData);
  afree(structure);
}

int tripwireTickCallback(void *structure)
{
  Structure *tripwire = structure;
  Player *fakePlayer = tripwire->fakePlayer;
  fakePlayer->
}

local void initWeapon(C2SPosition *packet)
{
  packet->weapon.type = W_BULLET;
  packet->weapon.level = 3;
}

local Structure *createTripwire(void)
{
  // Make stuff configurable
  StructureInfo info = {
    .id = 1,
    .callbackIntervalTicks = 100,
    .buildTimeTicks = 500,
    .createInstance = createTripwire,
    .destroyInstance = destroyTripwire,
    .tickCallback = tripwireTickCallback,
    .placedCallback = tripwirePlacedCallback,
    .destroyedCallback = tripwireDestroyedCallback
  };

  Structure *structure = amalloc(sizeof(*structure));
  structure->info = info;
  structure->extraData = amalloc(sizeof(C2SPosition));
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

    return MM_OK;
  }
  else if (action == MM_UNLOAD)
  {
    releaseInterfaces();
    return MM_OK;
  }
  else if (action == MM_ATTACH)
  {
    return MM_OK;
  }
  else if (action == MM_DETACH)
  {
    return MM_OK;
  }

  return MM_FAIL;
}