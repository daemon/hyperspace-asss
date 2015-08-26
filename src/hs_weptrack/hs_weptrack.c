#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hs_rtree.h"
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
local Irtree *rt;

local int adkey = -1;

typedef struct ArenaData
{
  pthread_cond_t callbacksNotEmpty;
  pthread_mutex_t callbacksMtx;
  pthread_mutexattr_t callbacksMtxAttr;
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

typedef enum ComputeRetCode
{
  COMPUTE_CONTINUE,
  COMPUTE_DEAD,
  COMPUTE_WALL_HIT,
  COMPUTE_TIMEOUT
} ComputeRetCode;

typedef enum TrackerType
{
  RECT_COLLISION,
  WALL_COLLISION,
  PLAYER_COLLISION,
  ANY_PLAYER_COLLISION,
} TrackerType;

typedef struct RectCollisionTracker
{
  TrackerType type;
  WepTrackRect bounds;
  bool shouldRemove;
} RectCollisionTracker;

typedef struct WallCollisionTracker
{
  TrackerType type;
  WepTrackRect bounds;
} WallCollisionTracker;

typedef struct PlayerCollisionTracker
{
  TrackerType type;
  Player *player;
} PlayerCollisionTracker;

typedef struct AnyPlayerCollisionTracker
{
  TrackerType type;
  WepTrackRect bounds;
} AnyPlayerCollisionTracker;

typedef union CollisionTracker
{
  TrackerType type;
  RectCollisionTracker rcTracker;
  WallCollisionTracker wcTracker;
  PlayerCollisionTracker pcTracker;
  AnyPlayerCollisionTracker apcTracker;
} CollisionTracker;

typedef struct WeaponsCbInfo
{
  Arena *arena;
  LinkedList *trackers;
  WepTrackInfo info;
  int key;
  pthread_mutex_t mtx;
} WeaponsCbInfo;

typedef struct WeaponState
{
  LinkedList *particles;
} WeaponState;

// Prototypes
local void AddWallCollision(WepTrackRect bounds, int key);
local void AddAnyPlayerCollision(WepTrackRect bounds, int key);
local void AddRectCollision(WepTrackRect bounds, bool shouldRemove, int key);
local void getInterfaces(void);
local bool checkInterfaces(void);
local void releaseInterfaces(void);
local inline bool WithinBounds(WepTrackRect *rect, int x, int y);
local void AddRectCollision(WepTrackRect bounds, bool shouldRemove, int key);
local void AddPlayerCollision(Player *player, int key);
local inline int ConvertToTrackingType(int ppkType);
local int RegWepTracking(Arena *arena, WepTrackInfo info);
local WeaponsCbInfo *findCallback(Arena *arena, int key);
local Arena *findArena(int key);
local void UnregWepTracking(int key);
local inline int getSpeed(Arena *arena, Player *p, struct Weapons *weapons);
local inline int getShipRadius(Player *p);
local inline int getAliveTime(Arena *arena, int type);
local Player *playerObstructing(Player *shooter, struct C2SPosition *wepPos);
local ComputeRetCode computeNextWeapons(WeaponParticle *ws, int stepTicks);
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
  rt = mm->GetInterface(I_RTREE, ALLARENAS);
  pd = mm->GetInterface(I_PLAYERDATA, ALLARENAS);
}

local bool checkInterfaces(void)
{
  if (aman && chat && cfg && db && fake && game && items && map && ml && net && pd && rt)
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
  mm->ReleaseInterface(rt);
  mm->ReleaseInterface(net);
  mm->ReleaseInterface(pd);
} 

local inline bool WithinBounds(WepTrackRect *rect, int x, int y)
{
  return x >= rect->x1 && x <= rect->x2 && y >= rect->y1 && y <= rect->y2;
}

local void AddAnyPlayerCollision(WepTrackRect bounds, int key)
{
  Arena *arena = findArena(key);
  if (!arena)
    return;

  WeaponsCbInfo *cbInfo = findCallback(arena, key);
  if (!cbInfo)
    return;

  AnyPlayerCollisionTracker *tracker = amalloc(sizeof(*tracker));
  tracker->type = ANY_PLAYER_COLLISION;
  tracker->bounds = bounds;

  pthread_mutex_lock(&cbInfo->mtx);
  LLAdd(cbInfo->trackers, tracker);
  pthread_mutex_unlock(&cbInfo->mtx); 
}

local void AddWallCollision(WepTrackRect bounds, int key)
{
  Arena *arena = findArena(key);
  if (!arena)
    return;

  WeaponsCbInfo *cbInfo = findCallback(arena, key);
  if (!cbInfo)
    return;

  WallCollisionTracker *tracker = amalloc(sizeof(*tracker));
  tracker->type = WALL_COLLISION;
  tracker->bounds = bounds;

  pthread_mutex_lock(&cbInfo->mtx);
  LLAdd(cbInfo->trackers, tracker);
  pthread_mutex_unlock(&cbInfo->mtx); 
}

local void AddRectCollision(WepTrackRect bounds, bool shouldRemove, int key)
{
  Arena *arena = findArena(key);
  if (!arena)
    return;

  WeaponsCbInfo *cbInfo = findCallback(arena, key);
  if (!cbInfo)
    return;

  RectCollisionTracker *tracker = amalloc(sizeof(*tracker));
  tracker->type = RECT_COLLISION;
  tracker->shouldRemove = shouldRemove;
  tracker->bounds = bounds;

  pthread_mutex_lock(&cbInfo->mtx);
  LLAdd(cbInfo->trackers, tracker);
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

  PlayerCollisionTracker *tracker = amalloc(sizeof(*tracker));
  tracker->type = PLAYER_COLLISION;
  tracker->player = player;

  pthread_mutex_lock(&cbInfo->mtx);
  LLAdd(cbInfo->trackers, tracker);
  pthread_mutex_unlock(&cbInfo->mtx);
}

local inline int ConvertToTrackingType(int ppkType)
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
  cbInfo->trackers = LLAlloc();
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

  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  pthread_mutex_lock(&adata->callbacksMtx);

  WeaponsCbInfo *cb = findCallback(arena, key);
  if (!cb)
    return;

  LLRemove(adata->callbackInfos, cb);
  LLFree(cb->trackers);
  pthread_mutex_destroy(&cb->mtx);
  afree(cb);

  pthread_mutex_unlock(&adata->callbacksMtx);
}

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

local inline int getAliveTime(Arena *arena, int type)
{
  ArenaData *adata = P_ARENA_DATA(arena, adkey);
  if (type == W_BULLET || type == W_BOUNCEBULLET)
    return adata->bulletAliveTime;
  else if (type == W_BOMB || type == W_THOR)
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

local WeaponState *allocWeaponState(Player *p, struct C2SPosition *pos)
{
  // TODO multifire, bursts, etc
  LinkedList *ll = LLAlloc();
  WeaponParticle *wp = amalloc(sizeof(*wp));
  wp->shooter = p;

  double xratio = cos(((pos->rotation * 9) - 90) * M_PI / 180);
  double yratio = sin(((pos->rotation * 9) - 90) * M_PI / 180);

  int wepSpeed = getSpeed(p->arena, p, pos);
  wp->xspeed = pos->xspeed + xratio * wepSpeed;
  wp->yspeed = pos->yspeed + yratio * wepSpeed;

  wp->x = pos->x;
  wp->y = pos->y;

  wp->type = pos->weapon.type;
  wp->time = pos->time;

  LLAdd(ll, wp);

  WeaponState *ws = amalloc(sizeof(*ws));
  ws->particles = ll;
  return ws;
}

local ComputeRetCode computeNextWeapons(WeaponParticle *wp, int stepTicks)
{
  ticks_t currentTicks = current_ticks();
  int aliveTime = getAliveTime(wp->shooter->arena, wp->type);
  if (TICK_GT(currentTicks, TICK_MAKE(wp->time + aliveTime)))
    return COMPUTE_TIMEOUT;

  double stepFraction = (((double) stepTicks) / 1000);

  int dx = stepFraction * wp->xspeed;
  int dy = stepFraction * wp->yspeed;

  int tile = map->GetTile(wp->shooter->arena, (wp->x + dx) >> 4, (wp->y + dy) >> 4);

  if (tile <= TILE_END && tile >= TILE_START && wp->type != W_THOR)
    return COMPUTE_WALL_HIT; // TODO: implement bouncing stuff

  wp->x += dx;
  wp->y += dy;

  return COMPUTE_CONTINUE;
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
    Link *l2;
    CollisionTracker *tracker;
    pthread_mutex_lock(&cbInfo->mtx);
    FOR_EACH(cbInfo->trackers, tracker, l2)
    {
      if (tracker->type == PLAYER_COLLISION && tracker->pcTracker.player == p)
      {
        LLRemove(cbInfo->trackers, tracker);
        afree(tracker);
        continue;
      }
    }
    pthread_mutex_unlock(&cbInfo->mtx);
  }
  pthread_mutex_unlock(&adata->callbacksMtx);
}

/** Returns true if weapon state should be removed */
local inline bool dispatchCallbacks(WeaponParticle *particle, ArenaData *adata, ComputeRetCode computeRc, 
  RTree playerTree, bool removeWs)
{
  TrackEvent event = {
    GENERAL_TRACKING_EVENT,
    0, particle
  };

  Link *link;    
  WeaponsCbInfo *cbInfo;
  // TODO make more efficient so people don't call you a jackass
  FOR_EACH(adata->callbackInfos, cbInfo, link) 
  {
    if (!(ConvertToTrackingType(particle->type) & cbInfo->info.trackingType && 
      WithinBounds(&cbInfo->info.bounds, particle->x, particle->y)))
      continue;

    event.type = GENERAL_TRACKING_EVENT;
    event.trackKey = cbInfo->key;
    cbInfo->info.callback(&event);
    
    Link *link2;
    CollisionTracker *tracker;        
    pthread_mutex_lock(&cbInfo->mtx);
    FOR_EACH(cbInfo->trackers, tracker, link2)
    {
      switch (tracker->type)
      {
      case WALL_COLLISION:
        if (computeRc == COMPUTE_WALL_HIT && WithinBounds(&tracker->wcTracker.bounds, particle->x, particle->y))
        {
          event.type = WALL_COLLISION_EVENT; 
          cbInfo->info.callback(&event);
        }
        break;

      case RECT_COLLISION:
        if (WithinBounds(&tracker->rcTracker.bounds, particle->x, particle->y))
        {
          event.type = RECT_COLLISION_EVENT;
          cbInfo->info.callback(&event);
          removeWs = removeWs || tracker->rcTracker.shouldRemove;
        }
        break;

      case PLAYER_COLLISION:
        if (removeWs)
          break;

        Player *tp = tracker->pcTracker.player;
        int shipRadius = getShipRadius(tp);
        WepTrackRect playerBounds = { 
          tp->position.x - shipRadius, tp->position.y - shipRadius,
          tp->position.x + shipRadius, tp->position.y + shipRadius  
        };

        if (tp->p_freq != particle->shooter->p_freq && WithinBounds(&playerBounds, particle->x, particle->y))
        {
          event.type = PLAYER_COLLISION_EVENT;
          event.data.collidedPlayer = tp;
          cbInfo->info.callback(&event);
          removeWs = true;
        }
        break;

      case ANY_PLAYER_COLLISION:
        if (removeWs)
          break;
        else if (!WithinBounds(&tracker->apcTracker.bounds, particle->x, particle->y))
          break;

        LinkedList collidedPlayers = rt->RTreeFindByPoint(&playerTree, particle->x, particle->y);
        if (LLIsEmpty(&collidedPlayers))
        {
          LLEmpty(&collidedPlayers);
          break;
        }

        Link *l3;
        Player *p;
        FOR_EACH(&collidedPlayers, p, l3)
        {
          if (p->p_freq != particle->shooter->p_freq)
            break;
        }
        
        event.type = PLAYER_COLLISION_EVENT;
        event.data.collidedPlayer = p;
        cbInfo->info.callback(&event);

        LLEmpty(&collidedPlayers);
        removeWs = true;
        break;
      default:
        break;
      }
    }
    pthread_mutex_unlock(&cbInfo->mtx);
  }

  return removeWs;
}

local void *trackLoop(void *arena)
{
  ArenaData *adata = P_ARENA_DATA((Arena *) arena, adkey);
  pthread_mutex_lock(&adata->callbacksMtx);
  while (true)
  {
    while (LLIsEmpty(adata->callbackInfos) || LLIsEmpty(adata->trackedWeapons))
      pthread_cond_wait(&adata->callbacksNotEmpty, &adata->callbacksMtx);

    ticks_t timeMillisA = current_millis();

    RTree playerTree = RTREE_INITIALIZER;
    Link *link;
    Player *player;
    pd->Lock();
    FOR_EACH_PLAYER(player)
    {
      if (player->arena != arena ||
        player->arena->specfreq == player->p_freq)
        continue;
      
      int shipRadius = getShipRadius(player);
      WepTrackRect playerRect = {
        player->position.x - shipRadius, player->position.y - shipRadius,
        player->position.x + shipRadius, player->position.y + shipRadius
      };
      rt->RTreeAdd(&playerTree, playerRect, player);
    }

    Link *l;
    WeaponState *ws;
    FOR_EACH(adata->trackedWeapons, ws, l)
    {
      bool removeWs = false;
      ComputeRetCode computeRc;

      Link *l2;
      WeaponParticle *particle;
      FOR_EACH(ws->particles, particle, l2)
      {
        if ((computeRc = computeNextWeapons(particle, TRACK_TIME_RESOLUTION)) == COMPUTE_TIMEOUT)
        {
          LLRemove(adata->trackedWeapons, ws);
          LLFree(ws->particles);
          afree(ws);
          break;
        } else if (computeRc == COMPUTE_WALL_HIT)
          removeWs = true;

        removeWs = removeWs || dispatchCallbacks(particle, adata, computeRc, playerTree, removeWs);

        LinkedList damagedPlayers = rt->RTreeFindByPoint(&playerTree, particle->x, particle->y);
        if (!LLIsEmpty(&damagedPlayers) || removeWs)
        {        
          LLRemove(adata->trackedWeapons, ws);
          LLFree(ws->particles);
          afree(ws);
          break;
        }
        LLEmpty(&damagedPlayers);
      }
    }

    pd->Unlock();
    rt->RTreeDeinit(&playerTree);
    // TODO make timestep larger if not sleep
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
  if (LLIsEmpty(adata->callbackInfos))
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
      WeaponState *ws = allocWeaponState(p, pos);      
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

local Iweptrack wepTrackInt = {
  INTERFACE_HEAD_INIT(I_WEPTRACK, "weptrack")
  RegWepTracking, UnregWepTracking, AddRectCollision, AddPlayerCollision, 
  AddWallCollision, AddAnyPlayerCollision, ConvertToTrackingType, WithinBounds
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

    pthread_mutexattr_init(&adata->callbacksMtxAttr);
    pthread_mutexattr_settype(&adata->callbacksMtxAttr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&adata->callbacksMtx, &adata->callbacksMtxAttr);

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