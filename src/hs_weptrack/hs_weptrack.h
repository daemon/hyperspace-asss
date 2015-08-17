#ifndef HS_WEPTRACK_H_
#define HS_WEPTRACK_H_

#include <stdbool.h>

#include "asss.h"
#include "fake.h"
#include "hscore.h"
#include "hscore_database.h"
#include "packets/ppk.h"

#define TRACK_BULLET          1
#define TRACK_BOUNCE_BULLET   2
#define TRACK_BOMB            4
#define TRACK_THOR            8
#define TRACK_ALL             0xFF

#define TRACK_WALL_COLLIDE    1
#define TRACK_PLAYER_COLLIDE  2

/* Time in ticks between tracking updates */
#define TRACK_TIME_RESOLUTION 15

#define I_WEPTRACK "weptrack-89"

typedef void (*TrackWeaponsCb)(Arena *arena, Player *shooter, struct C2SPosition *pos);

typedef struct WepTrackRect
{
  int x1, y1, x2, y2;
} WepTrackRect;

typedef struct CollisionCbInfo
{
  TrackWeaponsCb callback;
  WepTrackRect bounds;
  bool shouldRemove;
} CollisionCbInfo;

typedef struct WepTrackInfo
{
  /* The boundaries of tracking. Should be small (TM), since tracking can be expensive */
  WepTrackRect bounds;
  Arena *arena;

  /* Use TRACK_BULLET, TRACK_BOMB, etc; OR them together for multiple */
  int trackingType;
} WepTrackInfo;

typedef struct Iweptrack
{
  INTERFACE_HEAD_DECL

  /** Register weapons tracking callback. 
    * @param callback gets called every TRACK_TIME_RESOLUTION ticks when there are 
    * weapons fired within the boundaries of info.
    * @param key is used to unregister a callback  
    * @return a key that uniquely identifies this registration */
  int (*RegWepTracking)(WepTrackInfo info, TrackWeaponsCb callback);

  /* Unregisters the callback associated with key */
  void (*UnregWepTracking)(Arena *arena, int key);

  /* Adds collision checking callback to the weapons tracker previously registered 
   * using RegWepTracing.
   * @param arena the arena
   * @param key the unique identifier returned by RegWepTracking
   * @param collisionCb the callback, called every time a collision occurs with the box
   * @param shouldRemove if true, removes the weapon object (ie bullet) from 
   *  being tracked further upon collision */
  void (*AddCollisionCb)(Arena *arena, CollisionCbInfo info, int key);

  /* Converts packets/ppk.h W_* weapon types to tracking types */
  int (*ConvertToTrackingType)(int ppkWeaponType);

  bool (*WithinBounds)(WepTrackRect *rect, int x, int y);
} Iweptrack;

#endif