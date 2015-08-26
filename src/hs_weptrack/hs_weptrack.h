#ifndef HS_WEPTRACK_H_
#define HS_WEPTRACK_H_

#include <stdbool.h>

#include "asss.h"
#include "fake.h"
#include "hscore.h"
#include "hscore_database.h"
#include "packets/ppk.h"
#include "hs_rtree.h"

#define TRACK_BULLET          1
#define TRACK_BOUNCE_BULLET   2
#define TRACK_BOMB            4
#define TRACK_THOR            8
#define TRACK_ALL             0xFF

/* Time in ticks between tracking updates */
#define TRACK_TIME_RESOLUTION 8

#define I_WEPTRACK "weptrack-89"

typedef struct WeaponParticle
{
  unsigned int time;
  int xspeed;
  int yspeed;
  int x;
  int y;

  /** The type dictated by ppk.h */
  int type;
  Player *shooter;
} WeaponParticle;

typedef struct TrackEvent
{
  enum track_event_enum
  {  
    /** General tracking events are fired whenever weapons move 
     * This is probably too much for most applications */
    GENERAL_TRACKING_EVENT,

    /** Fired when a collision occurs with a specified rectangle, which must be
     * within the general tracking rectangle in RegWepTracking */
    RECT_COLLISION_EVENT,

    /** Fired when a player collision occurs within the general tracking 
     * rectangle. You must register the player to watch for this event in
     * the appropriate function.
     * Player *collidedPlayer is now valid and is the player that collided */
    PLAYER_COLLISION_EVENT,

    /** Fired when a wall collision occurs within a specified rectangle, which must be
     * within the general tracking rectangle in RegWepTracking */
    WALL_COLLISION_EVENT
  } type;

  struct WeaponParticle *weapon;
  int trackKey;

  union
  {
    // Valid for player collide event
    Player *collidedPlayer;
    // Put other event specific data here...
  } data;
} TrackEvent;

typedef void (*TrackWeaponsCb)(const TrackEvent *event);
typedef RTreeRect WepTrackRect;

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

  /** Unregisters the callback associated with key */
  void (*UnregWepTracking)(int key);

  /** Adds a collision checking bounds to the weapons tracker previously
   * registered using RegWepTracking.
   * @param key the unique identifier returned by RegWepTracking
   * @param removeWeapon if true, removes the weapon object (ie bullet) from 
   *  being further tracked upon collision */
  void (*AddRectCollision)(WepTrackRect bounds, bool removeWeapon, int key);

  /** Adds collision checking for a player to the weapons tracker registered
   * using RegWepTracking. Removes the weapon object (ie bullet) from being
   * tracked further upon collision.
   * This can be used to track damage for fake players.
   * @param player the player
   * @param key the unique identifier returned by RegWepTracking */
  void (*AddPlayerCollision)(Player *player, int key);  
  
  /** Adds collision checking for a wall within bounds to the tracker assigned
   * to key. Removes the weapon object (ie bullet) from being tracked further 
   * upon collision.
   * This can be used to help track bomb damage for fake players.
   * @param bounds the region to watch for weapon wall collisions
   * @param key the unique identifier returned by RegWepTracking */
  void (*AddWallCollision)(WepTrackRect bounds, int key);

  /** Adds collision checking for any player within bounds to the weapons 
   *  tracker registered using RegWepTracking. Removes the weapon object (ie 
   * bullet) from being tracked further upon collision.
   * @param bounds the region to watch for player weapon collisions
   * @param key the unique identifier returned by RegWepTracking */
  void (*AddAnyPlayerCollision)(WepTrackRect bounds, int key);  
  // TODO add fucking mechanism to remove collision trackers

  /** Converts packets/ppk.h W_* weapon types to tracking types */
  int (*ConvertToTrackingType)(int ppkWeaponType);

  bool (*WithinBounds)(WepTrackRect *rect, int x, int y);
} Iweptrack;

#endif