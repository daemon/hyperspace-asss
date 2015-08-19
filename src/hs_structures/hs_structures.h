#ifndef HS_STRUCTURES_H_
#define HS_STRUCTURES_H_

#include <stdbool.h>

#include "asss.h"
#include "fake.h"
#include "hscore.h"
#include "hscore_database.h"

// Item properties
local const char *ID_PROP = "structureidmask";
local const char *CAN_BUILD_PROP = "canbuild";
local const char *BUILD_DELAY_PROP = "builddelay";
local const char *MAXIMUM_BUILDS_PROP = "buildmax";

struct Structure;

typedef struct StructureInfo
{
  // The unique ID of the structure, MUST be power of 2
  int id;

  // Interval at which tickCallback is called
  unsigned int callbackIntervalTicks;
  unsigned int buildTimeTicks;

  struct Structure *(*createInstance)(void);
  void (*destroyInstance)(struct Structure *structure);

  bool (*canBuild)(Player *builder);
  int (*tickCallback)(void *structure);
  void (*placedCallback)(struct Structure *structure, Player *builder);
  void (*destroyedCallback)(struct Structure *structure, Player *killer);
} StructureInfo;

typedef struct Structure
{
  StructureInfo info;
  Player *fakePlayer;
  Player *owner;
  void *extraData;
} Structure;

#define I_STRUCTMAN "structman-173"

typedef struct Istructman
{
  INTERFACE_HEAD_DECL
  bool (*RegisterStructure)(Arena *arena, StructureInfo *structure);
  void (*UnregisterStructure)(Arena *arena, int id);
} Istructman;

#endif