#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hs_weptrack.h"
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

local int adkey = -1;

typedef struct ArenaData
{
  pthread_cond_t callbacksNotEmpty;
  pthread_mutex_t callbacksMtx;
  pthread_t trackLoopTh;
  LinkedList *callbackInfos;
  LinkedList *trackedWeapons; // Use kd-tree?

  int bulletAliveTime;
  int bombAliveTime;
} ArenaData;

typedef struct WeaponsCbInfo
{
  TrackWeaponsCb callback;
  LinkedList *collisionCbInfos;
  WepTrackInfo info;
  int key;
} WeaponsCbInfo;

typedef struct WeaponsState
{
  Arena *arena;
  Player *player;
  struct C2SPosition *weapons;
} WeaponsState;

local WeaponsCbInfo *findCallback(Arena *arena, int key);

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
}

local bool checkInterfaces(void)
{
  if (aman && chat && cfg && db && fake && game && items && map && ml && net && pd)
    return true;
  return false;
}

local void releaseInterfaces(void)
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

bool WithinBounds(WepTrackRect *rect, int x, int y)
{
  return x >= rect->x1 && x <= rect->x2 && y >= rect->y1 && y <= rect->y2;
}

local void AddCollisionCb(Arena *arena, CollisionCbInfo colCbInfo, int key)
{
  WeaponsCbInfo *cbInfo = findCallback(arena, key);
  CollisionCbInfo *cCbInfo = amalloc(sizeof(*cCbInfo));

  memcpy(cCbInfo, &colCbInfo, sizeof(*cCbInfo));
  LLAdd(cbInfo->collisionCbInfos, cCbInfo);
}

local int ConvertToTrackingType(int ppkType)
{
  if (ppkType == W_BULLET)
    return TRACK_BULLET;
  else if (ppkType == W_BOUNCEBULLET)
    return TRACK_BOUNCE_BULLET;
  else if (ppkType == W_BOMB)
    return TRACK_BOMB;
  else if (ppkType == W_THOR)
    return TRACK_THOR;
  else
    return 0;
}

local int RegWepTracking(WepTrackInfo info, TrackWeaponsCb callback)
{
  WeaponsCbInfo *cbInfo = amalloc(sizeof(*cbInfo));
  cbInfo->callback = callback;
  cbInfo->info = info;
  cbInfo->collisionCbInfos = LLAlloc();

  ArenaData *adata = P_ARENA_DATA(info.arena, adkey);
  pthread_mutex_lock(&adata->callbacksMtx);

  static int id = 0;
  int key = ++id;
  cbInfo->key = key;

  LLAdd(adata->callbackInfos, cbInfo);

  pthread_cond_signal(&adata->callbacksNotEmpty);
  pthread_mutex_unlock(&adata->callbacksMtx);

  return key;
}

local WeaponsCbInfo *findCallback(Arena *arena, int key)
{
  Link *link;
  WeaponsCbInfo *cbInfo;

  ArenaData *adata = P_ARENA_DATA(arena, adkey);

  pthread_mutex_lock(&adata->callbacksMtx);
  FOR_EACH(adata->callbackInfos, cbInfo, link)
  {
    if (cbInfo->key == key)
    {
      pthread_mutex_unlock(&adata->callbacksMtx);
      return cbInfo;
    }
  }

  pthread_mutex_unlock(&adata->callbacksMtx);
  return NULL;
}

local void UnregWepTracking(Arena *arena, int key)
{
  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  WeaponsCbInfo *cb = findCallback(arena, key);
  if (!cb)
    return;

  pthread_mutex_lock(&adata->callbacksMtx);

  LLRemove(adata->callbackInfos, cb);
  LLFree(cb->collisionCbInfos);
  afree(cb);

  pthread_mutex_unlock(&adata->callbacksMtx);
}

local Iweptrack wepTrackInt = {
  INTERFACE_HEAD_INIT(I_WEPTRACK, "weptrack")
  RegWepTracking, UnregWepTracking, AddCollisionCb, ConvertToTrackingType, WithinBounds
};

local inline int getSpeed(Arena *arena, Player *p, struct Weapons *weapons)
{
  // TODO do all config init at module load
  if (weapons->type == W_BULLET || weapons->type == W_BOUNCEBULLET)
    return cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[(int) p->p_ship], "BulletSpeed", 10);
  else if (weapons->type == W_BOMB || weapons->type == W_THOR)
    return cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[(int) p->p_ship], "BombSpeed", 10);
  else
    return 0;
}

local inline int getShipRadius(Player *p)
{
  return cfg->GetInt(p->arena->cfg, cfg->SHIP_NAMES[(int) p->p_ship], "Radius", 14);
}

local inline int getAliveTime(Arena *arena, struct Weapons *weapons)
{
  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  if (weapons->type == W_BULLET || weapons->type == W_BOUNCEBULLET)
    return adata->bulletAliveTime;
  else if (weapons->type == W_BOMB || weapons->type == W_THOR)
    return adata->bombAliveTime;
  else
    return 0;
}

local Player *playerObstructing(Player *shooter, struct C2SPosition *wepPos)
{
  Player *player;
  Link *link;
  FOR_EACH_PLAYER(player)
  {
    if (player->arena != shooter->arena ||
      player->arena->specfreq == player->p_freq ||
      player->p_freq == shooter->p_freq)
      continue;
    else 
    {
      int shipRadius = getShipRadius(player);
      if (abs(player->position.x - wepPos->x) < shipRadius &&
        abs(player->position.y - wepPos->y) < shipRadius)
        return player;
    }
  }

  return NULL;
}

local bool computeNextWeapons(Arena *arena, Player *player, struct C2SPosition *pos, int stepTicks)
{
  ticks_t currentTicks = current_ticks();
  int aliveTime = getAliveTime(arena, &pos->weapon);
  if (TICK_GT(currentTicks, TICK_MAKE(pos->time + aliveTime))) // TICK macros didn't work
    return false;

  double stepFraction = (((double) stepTicks) / 1000);
  double xratio = cos(((pos->rotation * 9) - 90) * M_PI / 180);
  double yratio = sin(((pos->rotation * 9) - 90) * M_PI / 180);
  int wepSpeed = getSpeed(arena, player, &pos->weapon);

  int dx = stepFraction * (pos->xspeed + xratio * wepSpeed);
  int dy = stepFraction * (pos->yspeed + yratio * wepSpeed);

  int tile = map->GetTile(arena, (pos->x + dx) >> 4, (pos->y + dy) >> 4);

  if (tile <= TILE_END && tile >= TILE_START && pos->weapon.type != W_THOR)
    return false; // TODO: implement bouncing stuff

  pos->x += dx;
  pos->y += dy;

  return true;
}

local void *trackLoop(void *arena)
{
  ArenaData *adata = P_ARENA_DATA((Arena *) arena, adkey);
  pthread_mutex_lock(&adata->callbacksMtx);
  while (true)
  {
    while (!LLCount(adata->callbackInfos) || !LLCount(adata->trackedWeapons))
      pthread_cond_wait(&adata->callbacksNotEmpty, &adata->callbacksMtx);

    Link *l;
    WeaponsState *ws;
    FOR_EACH(adata->trackedWeapons, ws, l)
    {
      bool removeWs = false;
      if (!computeNextWeapons(ws->arena, ws->player, ws->weapons, TRACK_TIME_RESOLUTION))
      {
        LLRemove(adata->trackedWeapons, ws);
        afree(ws->weapons);
        afree(ws);
        continue;
      }

      Link *link;    
      WeaponsCbInfo *cbInfo;
      FOR_EACH(adata->callbackInfos, cbInfo, link) 
      {
        if (ConvertToTrackingType(ws->weapons->weapon.type) & cbInfo->info.trackingType && 
          WithinBounds(&cbInfo->info.bounds, ws->weapons->x, ws->weapons->y))
        {
          if (cbInfo->callback)
            cbInfo->callback(cbInfo->info.arena, ws->player, ws->weapons);

          Link *link2;
          CollisionCbInfo *cCbInfo;
          FOR_EACH(cbInfo->collisionCbInfos, cCbInfo, link2)
          {
            if (WithinBounds(&cCbInfo->bounds, ws->weapons->x, ws->weapons->y))
            {
              cCbInfo->callback(cbInfo->info.arena, ws->player, ws->weapons);
              removeWs = removeWs || cCbInfo->shouldRemove;
            }
          }
        }
      }

      if (playerObstructing(ws->player, ws->weapons) || removeWs)
      {
        LLRemove(adata->trackedWeapons, ws);
        afree(ws->weapons);
        afree(ws);
      }
    }

    pthread_mutex_unlock(&adata->callbacksMtx);
    fullsleep(TRACK_TIME_RESOLUTION * 10);
    pthread_mutex_lock(&adata->callbacksMtx);
  }

  pthread_mutex_unlock(&adata->callbacksMtx);
  return NULL;
}

local void editPPK(Player *p, struct C2SPosition *pos)
{
  if (!IS_STANDARD(p))
    return;

  if (pos->weapon.type == W_NULL)
    return;

  ArenaData *adata = P_ARENA_DATA(p->arena, adkey);

  pthread_mutex_lock(&adata->callbacksMtx);
  if (!LLCount(adata->callbackInfos))
  {
    pthread_mutex_unlock(&adata->callbacksMtx);
    return;
  }

  WeaponsCbInfo *cbInfo;
  Link *link;

  FOR_EACH(adata->callbackInfos, cbInfo, link) 
  {
    if (ConvertToTrackingType(pos->weapon.type) & cbInfo->info.trackingType && 
      WithinBounds(&cbInfo->info.bounds, pos->x, pos->y))
    {
      WeaponsState *ws = amalloc(sizeof(*ws));
      ws->arena = p->arena;
      ws->player = p;

      struct C2SPosition *position = amalloc(sizeof(*position));
      memcpy(position, pos, sizeof(*pos));
      ws->weapons = position;
      LLAdd(adata->trackedWeapons, ws);
      pthread_cond_signal(&adata->callbacksNotEmpty);
      break;
    }
  }
  pthread_mutex_unlock(&adata->callbacksMtx);
}

local Appk PPKAdviser = {
  ADVISER_HEAD_INIT(A_PPK)

  editPPK,
  NULL
};

EXPORT int MM_hs_weptrack(int action, Imodman *mm_, Arena *arena)
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
    if (adkey == -1)
    {
      aman->FreeArenaData(adkey);
      releaseInterfaces();
    }

    mm->RegInterface(&wepTrackInt, ALLARENAS);
    return MM_OK;
  }
  else if (action == MM_UNLOAD)
  {
    releaseInterfaces();
    if (mm->UnregInterface(&wepTrackInt, ALLARENAS))
      return MM_FAIL;
    aman->FreeArenaData(adkey);
    return MM_OK;
  } else if (action == MM_ATTACH) {
    mm->RegAdviser(&PPKAdviser, arena);
    getInterfaces();
    if (!checkInterfaces())
    {
      releaseInterfaces();
      return MM_FAIL;
    }

    ArenaData *adata = P_ARENA_DATA(arena, adkey);

    adata->bulletAliveTime = cfg->GetInt(arena->cfg, "Bullet", "BulletAliveTime", 1000);
    adata->bombAliveTime = cfg->GetInt(arena->cfg, "Bomb", "BombAliveTime", 1000);
    pthread_cond_init(&adata->callbacksNotEmpty, NULL);
    pthread_mutex_init(&adata->callbacksMtx, NULL);
    pthread_create(&adata->trackLoopTh, NULL, trackLoop, arena);

    adata->callbackInfos = LLAlloc();
    adata->trackedWeapons = LLAlloc();

    return MM_OK;
  } else if (action == MM_DETACH) {
    if (adkey == -1)
      return MM_FAIL;

    mm->UnregAdviser(&PPKAdviser, arena);
    ArenaData *adata = P_ARENA_DATA(arena, adkey);

    pthread_cancel(adata->trackLoopTh);

    pthread_cond_destroy(&adata->callbacksNotEmpty);
    pthread_mutex_destroy(&adata->callbacksMtx);

    // Leaks mem
    LLFree(adata->callbackInfos);
    LLFree(adata->trackedWeapons);

    return MM_OK;
  }

  return MM_FAIL;
}