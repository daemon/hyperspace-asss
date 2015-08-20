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

#define COLLIDE_WALL    1
#define COLLIDE_PLAYER  2
#define COLLIDE_ALL     0xFF

/* Time in ticks between tracking updates */
#define TRACK_TIME_RESOLUTION 8

#define I_WEPTRACK "weptrack-89"

typedef struct TrackEvent
{
  enum track_event_enum
  {  
    /* General tracking events are fired whenever weapons move 
     * This is probably too much for most applications */
    GENERAL_TRACKING_EVENT,

    /* Fired when a collision occurs within a specified rectangle, which must be
     * within the general tracking rectangle in RegWepTracking */
    RECT_COLLISION_EVENT,

    /* Fired when a player collision occurs within the general tracking 
     * rectangle. You must register the player to watch for this event in
     * the appropriate function.
     * Player *collidedPlayer is now valid and is the player that collided */
    PLAYER_COLLISION_EVENT,

    /* Fired when a wall collision occurs within a specified rectangle, which must be
     * within the general tracking rectangle in RegWepTracking */
    WALL_COLLISION_EVENT
  } eventType;

  Arena *arena;
  Player *shooter;

  // The weapon position. TODO: change this to something other than C2SPosition
  struct C2SPosition *weaponPos;

  union
  {
    // Valid for player collide event
    Player *collidedPlayer;
    // Put other event specific data here...
  } data;
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
   * @param removeWeapon if true, removes the weapon object (ie bullet) from 
   *  being further tracked upon collision */
  void (*AddRectCollision)(WepTrackRect bounds, bool removeWeapon, int key);

  /* Adds collision checking for a player to the weapons tracker registered
   * using RegWepTracking. Removes the weapon object (ie bullet) from being
   * tracked further upon collision.
   * This can be used to track damage for fake players.
   * @param player the player
   * @param key the unique identifier returned by RegWepTracking */
  void (*AddPlayerCollision)(Player *player, int key);
  
  
  void (*AddWallCollision)(WepTrackRect bounds, int key);

  // TODO add fucking mechanism to remove collision trackers

  /* Converts packets/ppk.h W_* weapon types to tracking types */
  int (*ConvertToTrackingType)(int ppkWeaponType);

  bool (*WithinBounds)(WepTrackRect *rect, int x, int y);
} Iweptrack;

#endif