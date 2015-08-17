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
#define TRACK_TIME_RESOLUTION 8

#define I_WEPTRACK "weptrack-89"

typedef struct TrackEvent
{
  enum track_event_enum
  {  
    GENERAL_TRACKING_EVENT,
    RECT_COLLISION_EVENT,
    FAKE_PLAYER_COLLISION_EVENT
  } eventType;

  Arena *arena;
  Player *shooter;
  struct C2SPosition *weaponPos;
} TrackEvent;

typedef void (*TrackWeaponsCb)(const TrackEvent *event);

typedef struct WepTrackRect
{
  int x1, y1, x2, y2;
} WepTrackRect;

typedef struct WepTrackInfo
{
  /* The boundaries of tracking. Should be small (TM), since tracking can be expensive */
  WepTrackRect bounds;
  TrackWeaponsCb callback;

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
  int (*RegWepTracking)(Arena *arena, WepTrackInfo info);

  /* Unregisters the callback associated with key */
  void (*UnregWepTracking)(int key);

  /* Adds a collision checking bounds to the weapons tracker previously
   * registered using RegWepTracking.
   * @param key the unique identifier returned by RegWepTracking
   * @param shouldRemove if true, removes the weapon object (ie bullet) from 
   *  being tracked further upon collision */
  void (*AddRectCollision)(WepTrackRect bounds, bool shouldRemove, int key);

  /* Adds collision checking for a fake player to the weapons tracker registered
   * using RegWepTracking.
   * @param fake the fake
   * @param key the unique identifier returned by RegWepTracking */
  // void (*AddFakePlayerCollision)(Player *fake, int key);

  /* Converts packets/ppk.h W_* weapon types to tracking types */
  int (*ConvertToTrackingType)(int ppkWeaponType);

  bool (*WithinBounds)(WepTrackRect *rect, int x, int y);
} Iweptrack;

#endif