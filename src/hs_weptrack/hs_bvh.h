#ifndef HS_BVH_H_
#define HS_BVH_H_

#include <stdbool.h>

#include "asss.h"
#include "hscore.h"

#define I_BVH "bvh-78"

typedef struct BvhRect
{
  int x1, y1, x2, y2;
} BvhRect;

struct BvhNode;

typedef struct Bvh
{
  struct BvhNode *root;
} Bvh;

typedef struct Ibvh
{
  INTERFACE_HEAD_DECL

  void (*BvhInit)(Bvh *bvh);
  void (*BvhDeinit)(Bvh *bvh);

  void (*BvhAdd)(Bvh *bvh, BvhRect rect, void *data);
  void (*BvhRemove)(Bvh *bvh, void *data);

  LinkedList (*BvhFindByArea)(Bvh *bvh, BvhRect rect);
  LinkedList (*BvhFindByPoint)(Bvh *bvh, int x, int y);
} Ibvh;

#endif