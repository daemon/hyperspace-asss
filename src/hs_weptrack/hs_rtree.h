#ifndef HS_RTREE_H_
#define HS_RTREE_H_

#include <stdbool.h>

#include "asss.h"
#include "hscore.h"

#define I_BVH "rtree-78"
#define BVH_INITIALIZER { NULL }

typedef struct RTreeRect
{
  int x1, y1, x2, y2;
} RTreeRect;

struct RTreeNode;

typedef struct RTree
{
  struct RTreeNode *root;
} RTree;

typedef struct Irtree
{
  INTERFACE_HEAD_DECL

  void (*RTreeInit)(RTree *rtree);
  void (*RTreeDeinit)(RTree *rtree);

  void (*RTreeAdd)(RTree *rtree, RTreeRect rect, void *data);
  void (*RTreeRemove)(RTree *rtree, void *data);

  LinkedList (*RTreeFindByArea)(RTree *rtree, RTreeRect rect);
  LinkedList (*RTreeFindByPoint)(RTree *rtree, int x, int y);
} Irtree;

#endif