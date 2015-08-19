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

  // Settings for the 8 ships
  int bombSpeed[8];
  int bulletSpeed[8];
  int shipRadius[8];
} ArenaData;

// TODO unify collision callback interfaces

typedef struct CollisionInfo
{
  WepTrackRect bounds;
  bool shouldRemove;
} CollisionInfo;

typedef struct WeaponsCbInfo
{
  Arena *arena;
  LinkedList *collisionInfos;
  LinkedList *trackedPlayers;
  WepTrackInfo info;
  int key;
  pthread_mutex_t mtx;
} WeaponsCbInfo;

typedef struct WeaponsState
{
  Arena *arena;
  Player *player;
  struct C2SPosition *weapons;
  double xratio;
  double yratio;
  int weaponSpeed;
} WeaponsState;

// Prototypes
local void AddRectCollision(WepTrackRect bounds, bool shouldRemove, int key);
local void getInterfaces(void);
local bool checkInterfaces(void);
local void releaseInterfaces(void);
local bool WithinBounds(WepTrackRect *rect, int x, int y);
local void AddRectCollision(WepTrackRect bounds, bool shouldRemove, int key);
local void AddPlayerCollision(Player *player, int key);
local int ConvertToTrackingType(int ppkType);
local int RegWepTracking(Arena *arena, WepTrackInfo info);
local WeaponsCbInfo *findCallback(Arena *arena, int key);
local Arena *findArena(int key);
local void UnregWepTracking(int key);
local inline int getSpeed(Arena *arena, Player *p, struct Weapons *weapons);
local inline int getShipRadius(Player *p);
local inline int getAliveTime(Arena *arena, struct Weapons *weapons);
local Player *playerObstructing(Player *shooter, struct C2SPosition *wepPos);
local bool computeNextWeapons(WeaponsState *ws, int stepTicks);
local void playerActionCb(Player *p, int action, Arena *arena);
local void *trackLoop(void *arena);
local void editPPK(Player *p, struct C2SPosition *pos);

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

local bool WithinBounds(WepTrackRect *rect, int x, int y)
{
  return x >= rect->x1 && x <= rect->x2 && y >= rect->y1 && y <= rect->y2;
}

local void AddRectCollision(WepTrackRect bounds, bool shouldRemove, int key)
{
  Arena *arena = findArena(key);
  if (!arena)
    return;

  WeaponsCbInfo *cbInfo = findCallback(arena, key);
  if (!cbInfo)
    return;

  CollisionInfo *cCbInfo = amalloc(sizeof(*cCbInfo));
  cCbInfo->shouldRemove = shouldRemove;
  cCbInfo->bounds = bounds;

  pthread_mutex_lock(&cbInfo->mtx);
  LLAdd(cbInfo->collisionInfos, cCbInfo);
  pthread_mutex_unlock(&cbInfo->mtx);
}

local void AddPlayerCollision(Player *player, int key)
{
  Arena *arena = player->arena;
  if (!arena)
    return;

  WeaponsCbInfo *cbInfo = findCallback(arena, key);
  if (!cbInfo)
    return;

  pthread_mutex_lock(&cbInfo->mtx);
  LLAdd(cbInfo->trackedPlayers, player);
  pthread_mutex_unlock(&cbInfo->mtx);
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

local int RegWepTracking(Arena *arena, WepTrackInfo info)
{
  WeaponsCbInfo *cbInfo = amalloc(sizeof(*cbInfo));
  cbInfo->info = info;
  cbInfo->collisionInfos = LLAlloc();
  cbInfo->trackedPlayers = LLAlloc();
  cbInfo->arena = arena;
  pthread_mutex_init(&cbInfo->mtx, NULL);

  ArenaData *adata = P_ARENA_DATA(arena, adkey);
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

local Arena *findArena(int key)
{
  aman->Lock();
  Arena *arena;
  Link *link;
  FOR_EACH_ARENA(arena)
  {
    ArenaData *adata = P_ARENA_DATA(arena, adkey);
    if (!adata)
      continue;

    WeaponsCbInfo *cb = findCallback(arena, key);
    if (!cb)
      continue;

    aman->Unlock();
    return arena;
  }

  aman->Unlock();
  return NULL;
}

local void UnregWepTracking(int key)
{
  Arena *arena = findArena(key);
  if (!arena)
    return;

  WeaponsCbInfo *cb = findCallback(arena, key);
  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  pthread_mutex_lock(&adata->callbacksMtx);

  LLRemove(adata->callbackInfos, cb);
  LLFree(cb->collisionInfos);
  LLFree(cb->trackedPlayers);
  pthread_mutex_destroy(&cb->mtx);
  afree(cb);

  pthread_mutex_unlock(&adata->callbacksMtx);
}

local Iweptrack wepTrackInt = {
  INTERFACE_HEAD_INIT(I_WEPTRACK, "weptrack")
  RegWepTracking, UnregWepTracking, AddRectCollision, AddPlayerCollision, 
  ConvertToTrackingType, WithinBounds
};

local inline int getSpeed(Arena *arena, Player *p, struct Weapons *weapons)
{
  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  if (weapons->type == W_BULLET || weapons->type == W_BOUNCEBULLET)
    return adata->bulletSpeed[(int) p->p_ship];
  else if (weapons->type == W_BOMB || weapons->type == W_THOR)
    return adata->bulletSpeed[(int) p->p_ship];
  else
    return 0;
}

local inline int getShipRadius(Player *p)
{
  if (!p->arena)
    return 0;
  ArenaData *adata = P_ARENA_DATA(p->arena, adkey);
  return adata->shipRadius[(int) p->p_ship];
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
  pd->Lock();
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
      {
        pd->Unlock();
        return player;
      }
    }
  }

  pd->Unlock();
  return NULL;
}

local WeaponsState *allocWeaponsState(Player *p, struct C2SPosition *pos)
{
  WeaponsState *ws = amalloc(sizeof(*ws));
  ws->arena = p->arena;
  ws->player = p;

  struct C2SPosition *position = amalloc(sizeof(*position));
  memcpy(position, pos, sizeof(*pos));
  ws->weapons = position;

  ws->xratio = cos(((pos->rotation * 9) - 90) * M_PI / 180);
  ws->yratio = sin(((pos->rotation * 9) - 90) * M_PI / 180);
  ws->weaponSpeed = getSpeed(ws->arena, ws->player, &pos->weapon);

  return ws;
}

local bool computeNextWeapons(WeaponsState *ws, int stepTicks)
{
  struct C2SPosition *pos = ws->weapons;
  ticks_t currentTicks = current_ticks();
  int aliveTime = getAliveTime(ws->arena, &pos->weapon);
  if (TICK_GT(currentTicks, TICK_MAKE(pos->time + aliveTime)))
    return false;

  double stepFraction = (((double) stepTicks) / 1000);
  int wepSpeed = ws->weaponSpeed;

  int dx = stepFraction * (pos->xspeed + ws->xratio * wepSpeed);
  int dy = stepFraction * (pos->yspeed + ws->yratio * wepSpeed);

  int tile = map->GetTile(ws->arena, (pos->x + dx) >> 4, (pos->y + dy) >> 4);

  if (tile <= TILE_END && tile >= TILE_START && pos->weapon.type != W_THOR)
    return false; // TODO: implement bouncing stuff

  pos->x += dx;
  pos->y += dy;

  return true;
}

local void playerActionCb(Player *p, int action, Arena *arena)
{
  if (action != PA_LEAVEARENA)
    return;

  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  Link *l;
  WeaponsCbInfo *cbInfo;

  pthread_mutex_lock(&adata->callbacksMtx);
  FOR_EACH(adata->callbackInfos, cbInfo, l)
  {
    pthread_mutex_lock(&cbInfo->mtx);
    LLRemove(cbInfo->trackedPlayers, p);
    pthread_mutex_unlock(&cbInfo->mtx);
  }
  pthread_mutex_unlock(&adata->callbacksMtx);
}

local void *trackLoop(void *arena)
{
  ArenaData *adata = P_ARENA_DATA((Arena *) arena, adkey);
  pthread_mutex_lock(&adata->callbacksMtx);
  while (true)
  {
    while (!LLCount(adata->callbackInfos) || !LLCount(adata->trackedWeapons))
      pthread_cond_wait(&adata->callbacksNotEmpty, &adata->callbacksMtx);

    ticks_t timeMillisA = current_millis();

    Link *l;
    WeaponsState *ws;
    FOR_EACH(adata->trackedWeapons, ws, l)
    {
      bool removeWs = false;
      if (!computeNextWeapons(ws, TRACK_TIME_RESOLUTION))
      {
        LLRemove(adata->trackedWeapons, ws);
        afree(ws->weapons);
        afree(ws);
        continue;
      }

      TrackEvent event = {
        GENERAL_TRACKING_EVENT,
        (Arena *) arena,
        ws->player, ws->weapons
      };

      Link *link;    
      WeaponsCbInfo *cbInfo;
      // TODO make more efficient so people don't call you a jackass, ie r-tree, bloom filter
      FOR_EACH(adata->callbackInfos, cbInfo, link) 
      {
        // 
        if (!(ConvertToTrackingType(ws->weapons->weapon.type) & cbInfo->info.trackingType && 
          WithinBounds(&cbInfo->info.bounds, ws->weapons->x, ws->weapons->y)))
          continue;

        event.eventType = GENERAL_TRACKING_EVENT;
        cbInfo->info.callback(&event);
        
        Link *link2;
        CollisionInfo *cCbInfo;        
        pthread_mutex_lock(&cbInfo->mtx);
        FOR_EACH(cbInfo->collisionInfos, cCbInfo, link2)
        {
          if (WithinBounds(&cCbInfo->bounds, ws->weapons->x, ws->weapons->y))
          {
            event.eventType = RECT_COLLISION_EVENT;
            cbInfo->info.callback(&event);
            removeWs = removeWs || cCbInfo->shouldRemove;
          }
        }
        pthread_mutex_unlock(&cbInfo->mtx);

        if (removeWs)
        {
          LLRemove(adata->trackedWeapons, ws);
          afree(ws->weapons);
          afree(ws);
          continue;
        }

        Player *tp;
        pthread_mutex_lock(&cbInfo->mtx);
        FOR_EACH(cbInfo->trackedPlayers, tp, link2)
        {
          int shipRadius = getShipRadius(tp);
          WepTrackRect playerBounds = { 
            tp->position.x - shipRadius, tp->position.y - shipRadius,
            tp->position.x + shipRadius, tp->position.y + shipRadius  
          };

          if (tp->p_freq != ws->player->p_freq && WithinBounds(&playerBounds, ws->weapons->x, ws->weapons->y))
          {
            event.eventType = PLAYER_COLLISION_EVENT;
            event.data.collidedPlayer = tp;
            cbInfo->info.callback(&event);
            removeWs = true;
          }      
        }
        pthread_mutex_unlock(&cbInfo->mtx);
      }

      if (playerObstructing(ws->player, ws->weapons) || removeWs)
      {
        LLRemove(adata->trackedWeapons, ws);
        afree(ws->weapons);
        afree(ws);
      }
    }

    pthread_mutex_unlock(&adata->callbacksMtx);
    int delayMillis = 10 * TRACK_TIME_RESOLUTION - TICK_DIFF(current_millis(), timeMillisA);
    if (delayMillis > 0)
      fullsleep(delayMillis);
    
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
      WeaponsState *ws = allocWeaponsState(p, pos);      
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

    mm->RegCallback(CB_PLAYERACTION, playerActionCb, ALLARENAS);
    mm->RegInterface(&wepTrackInt, ALLARENAS);
    return MM_OK;
  }
  else if (action == MM_UNLOAD)
  {
    releaseInterfaces();
    mm->UnregCallback(CB_PLAYERACTION, playerActionCb, ALLARENAS);
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

    for (int i = 0; i < 8; ++i)
    {
      adata->bombSpeed[i] = cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[i], "BombSpeed", 10);
      adata->bulletSpeed[i] = cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[i], "BulletSpeed", 10);
      adata->shipRadius[i] = cfg->GetInt(arena->cfg, cfg->SHIP_NAMES[i], "Radius", 14);
    }

    adata->callbackInfos = LLAlloc();
    adata->trackedWeapons = LLAlloc();

    pthread_cond_init(&adata->callbacksNotEmpty, NULL);
    pthread_mutex_init(&adata->callbacksMtx, NULL);
    pthread_create(&adata->trackLoopTh, NULL, trackLoop, arena);

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