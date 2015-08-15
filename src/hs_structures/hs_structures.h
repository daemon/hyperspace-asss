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
  int id;
  unsigned int callbackIntervalTicks;
  unsigned int buildTimeTicks;
  struct Structure *(*createInstance)(void);
  void (*destroyInstance)(struct Structure *structure);
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

bool registerStructure(Arena *arena, StructureInfo *structure);
void unregisterStructure(Arena *arena, int id);

#endif