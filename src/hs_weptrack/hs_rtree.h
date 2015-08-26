#ifndef HS_RTREE_H_
#define HS_RTREE_H_

#include <stdbool.h>

#include "asss.h"
#include "hscore.h"

#define I_RTREE "rtree-78"
#define RTREE_INITIALIZER { NULL }

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

  /** Initializes the R-tree structure. The RTREE_INITIALIZER macro does the
   * same thing for static initialization. */
  void (*RTreeInit)(RTree *rtree);

  /** Frees the memory occupied by the individual nodes, but not the data */
  void (*RTreeDeinit)(RTree *rtree);

  /** Frees the memory occupied by the individual nodes, the data, and the tree */
  void (*RTreeFree)(RTree *rtree);

  /** Adds data associated with a rectangle to the specified R-tree.
   * @param rtree the R-tree to add the data to
   * @param rect the rectangle to associated data with
   * @param data the data */
  void (*RTreeAdd)(RTree *rtree, RTreeRect rect, void *data);

  /* Incomplete. Do not use */
  void (*RTreeRemove)(RTree *rtree, void *data);

  /** Returns a list of all the data intersecting with a specified rectangle.
   * LLEmpty should be called on the returned linked list; otherwise, it would
   * leak memory. Rectangles that share a side are considered intersecting.
   * @param rtree the R-tree to query
   * @param rect the rect check intersection 
   * @return the list of relevant data */
  LinkedList (*RTreeFindByRect)(RTree *rtree, RTreeRect rect);

  /** Returns a list of all the data that contains point (x, y). Points that lie
   * on the edge of a rectangle are regarded as contained.
   * @param rtree the R-tree to query
   * @param x the x coordinate
   * @param y the y coordinate 
   * @return the list of relevant data */
  LinkedList (*RTreeFindByPoint)(RTree *rtree, int x, int y);
} Irtree;

#endif