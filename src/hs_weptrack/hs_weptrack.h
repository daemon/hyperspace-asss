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

/* Time in ticks between tracking updates */
#define TRACK_TIME_RESOLUTION 10

#define I_WEPTRACK "weptrack-89"

typedef void (*TrackWeaponsCb)(Arena *arena, Player *player, struct C2SPosition *pos);

typedef struct WepTrackInfo
{
  /* The boundaries of tracking. Should be small (TM), since tracking can be expensive */
  int x1, y1, x2, y2;

  Arena *arena;
  Player *shooter;

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
    * @return true on success */
  void (*RegWepTracking)(WepTrackInfo info, TrackWeaponsCb callback, int key);

  /* Unregisters the callback associated with key */
  void (*UnregWepTracking)(Arena *arena, int key);

  /* Converts packets/ppk.h W_* weapon types to tracking types */
  int (*ConvertToTrackingType)(int ppkWeaponType);
} Iweptrack;

#endif