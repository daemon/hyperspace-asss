#ifndef HS_WEPTRACK_H_
#define HS_WEPTRACK_H_

#include "packets/ppk.h"

#define CB_TRACKWEAPONS "weptrack"
typedef void (*TrackWeapons)(Arena *arena, Player *player, struct S2CWeapons *weapons);

#endif