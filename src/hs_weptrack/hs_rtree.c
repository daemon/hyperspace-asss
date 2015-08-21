#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hs_rtree.h"

#define N_RTREE_ELEMENTS 4
#define RTREE_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define RTREE_MAX(a, b) (((a) > (b)) ? (a) : (b))

local Imodman *mm;

typedef struct RTreeNode
{
  RTreeRect bounds;
  struct RTreeNode *parent;

  size_t nChildren;
  struct RTreeNode *children[N_RTREE_ELEMENTS];

  bool leaf;
  void *data;
} RTreeNode;

// Prototypes
local void RTreeInit(RTree *rTree);
local void RTreeDeinit(RTree *rTree);
local void RTreeAdd(RTree *rTree, RTreeRect rect, void *data);
local void RTreeRemove(RTree *rTree, void *data);
local LinkedList RTreeFindByArea(RTree *rTree, RTreeRect rect);
local LinkedList RTreeFindByPoint(RTree *rTree, int x, int y);

local void freeRTreeNode(RTreeNode *rTreeNode);
local void rTreeNodeAdd(RTreeNode *rTreeNode, RTreeNode *child, bool overflow);
local void rTreeNodeMove(RTreeNode *root, RTreeNode *child);

local void freeRTreeNode(RTreeNode *rTreeNode)
{
  for (int i = 0; i < N_RTREE_ELEMENTS; ++i)
    if (rTreeNode->children[i])
      freeRTreeNode(rTreeNode->children[i]);
  
  if (rTreeNode->data)
    afree(rTreeNode->data);
  return;
}

local RTreeNode *createRTreeNode(RTreeRect bounds, void *data, RTreeNode *parent, bool leaf, RTreeNode **children)
{
  RTreeNode *node = amalloc(sizeof(*node));
  node->bounds = bounds;
  node->data = data;
  node->leaf = leaf;
  node->parent = parent;

  size_t nChildren = 0;
  for (int i = 0; i < N_RTREE_ELEMENTS; ++i)
    if (!children || !children[i])
      node->children[i] = NULL;
    else
    {
      node->children[i] = children[i];
      ++nChildren;
    }

  node->nChildren = nChildren;
  return node;
}

local inline int areaOf(RTreeRect bounds)
{
  return (bounds.x2 - bounds.x1) * (bounds.y2 - bounds.y1);
}

local inline RTreeRect minBB(RTreeRect a, RTreeRect b)
{
  int x1 = RTREE_MIN(a.x1, b.x1);
  int y1 = RTREE_MIN(a.y1, b.y1);
  int x2 = RTREE_MAX(a.x2, b.x2);
  int y2 = RTREE_MAX(a.y2, b.y2);
  RTreeRect rect = { x1, y1, x2, y2 };
  return rect;
}

// Quadratic method
local RTreeNode **findWorstPair(RTreeNode *node)
{
  RTreeNode **worst = amalloc(sizeof(*worst) * 2);
  int sumAreas = 0;
  for (int i = 0; i < N_RTREE_ELEMENTS; ++i)
    for (int j = 0; j < N_RTREE_ELEMENTS; ++j)
    {
      if (i == j)
        continue;
      int ijArea = areaOf(minBB(node->children[i]->bounds, node->children[j]->bounds));
      if (ijArea >= sumAreas)
      {
        worst[0] = node->children[i];
        worst[1] = node->children[j];
      }
    }
  
  return worst;
}

RTreeNode *insertHeuristic(RTreeNode *first, RTreeNode *second, RTreeNode *node)
{
  int origArea1 = areaOf(first->bounds);
  int origArea2 = areaOf(second->bounds);  
  int area1 = areaOf(minBB(node->bounds, first->bounds));
  int area2 = areaOf(minBB(node->bounds, second->bounds));

  if (first->nChildren == N_RTREE_ELEMENTS)
    return second;
  else if (second->nChildren == N_RTREE_ELEMENTS)
    return first;
  else if ((area1 - origArea1) > (area2 - origArea2))
    return second;
  else if ((area1 - origArea1) < (area2 - origArea2))
    return first;
  else if (area1 > area2)
    return second;
  else if (area1 < area2)
    return first;
  else if (first->nChildren > second->nChildren)
    return second;
  else
    return first;
}

local RTreeNode **splitRTreeNode(RTreeNode *node)
{
  RTreeNode **worstPair = findWorstPair(node);
  RTreeNode *origPair[2] = {
    createRTreeNode(worstPair[0]->bounds, worstPair[0]->data, worstPair[0], true, NULL),
    createRTreeNode(worstPair[1]->bounds, worstPair[1]->data, worstPair[1], true, NULL)
  };

  rTreeNodeMove(worstPair[0], origPair[0]);
  rTreeNodeMove(worstPair[1], origPair[1]);

  for (int i = 0; i < N_RTREE_ELEMENTS; ++i)
  {  
    if (node->children[i] == worstPair[0] || node->children[i] == worstPair[1])
      continue;
      
    rTreeNodeMove(insertHeuristic(worstPair[0], worstPair[1], node->children[i]), node->children[i]);
  }

  worstPair[0]->parent = NULL;
  worstPair[1]->parent = NULL;
  worstPair[0]->leaf = false;
  worstPair[1]->leaf = false;
  worstPair[0]->data = NULL;
  worstPair[1]->data = NULL;

  return worstPair;    
}

local inline void rTreeNodeMove(RTreeNode *parent, RTreeNode *child)
{
  parent->bounds = minBB(parent->bounds, child->bounds);
  parent->children[parent->nChildren++] = child;
  child->parent = parent;
}

local void rTreeNodeFix(RTreeNode *parent, RTreeNode *child)
{
  if (!parent)
    return;
  parent->bounds = minBB(child->bounds, parent->bounds);
  rTreeNodeFix(parent->parent, parent);
}

local void rTreeNodeAdd(RTreeNode *rTreeNode, RTreeNode *child, bool overflow) //RTreeRect rect, void *data)
{
  if (!rTreeNode->leaf && !overflow)
  {
    RTreeNode *minNode = NULL;
    int minExpansionArea = INT_MAX;
    for (int i = 0; i < N_RTREE_ELEMENTS; ++i)
    {
      if (!rTreeNode->children[i])
        break;

      int expansionArea = areaOf(minBB(rTreeNode->children[i]->bounds, child->bounds)) - 
        areaOf(rTreeNode->children[i]->bounds);
      if (expansionArea < minExpansionArea)
      {
        minExpansionArea = expansionArea;
        minNode = rTreeNode->children[i];
      }
    }

    rTreeNodeAdd(minNode, child, false);
  }
  else if (rTreeNode->nChildren < N_RTREE_ELEMENTS && (rTreeNode->leaf || overflow))
  {
    rTreeNode->bounds = minBB(rTreeNode->bounds, child->bounds);
    rTreeNode->children[rTreeNode->nChildren++] = child; // createRTreeNode(rect, data, rTreeNode, true, NULL);
    child->parent = rTreeNode;
    rTreeNodeFix(rTreeNode->parent, rTreeNode);
  }
  else 
  {
    RTreeNode **pair = splitRTreeNode(rTreeNode);
    // RTreeNode *node = createRTreeNode(rect, data, rTreeNode, true, NULL);
    rTreeNodeMove(insertHeuristic(pair[0], pair[1], child), child);    

    if (rTreeNode->parent)
    {
      rTreeNodeAdd(rTreeNode->parent, pair[0], true);
      rTreeNodeAdd(rTreeNode->parent, pair[1], true);
    } else {
      RTreeNode *children[2] = {pair[0], pair[1]};
      RTreeNode *node = createRTreeNode(minBB(rTreeNode->bounds, pair[0]->bounds), NULL, NULL, false, children);
      node->bounds = minBB(rTreeNode->bounds, pair[1]->bounds);
    }

    afree(pair);
    afree(rTreeNode);
  }
}

local void RTreeInit(RTree *rTree)
{
  rTree->root = NULL;
}

local void RTreeDeinit(RTree *rTree)
{
  if (rTree->root)
    freeRTreeNode(rTree->root);
  rTree->root = NULL;
}

local void RTreeAdd(RTree *rTree, RTreeRect rect, void *data)
{
  if (!rTree->root)
  {
    rTree->root = createRTreeNode(rect, data, NULL, true, NULL);
  } else {
    rTreeNodeAdd(rTree->root, createRTreeNode(rect, data, NULL, true, NULL), false);
  }
}

local void RTreeRemove(RTree *rTree, void *data)
{
  if (!rTree->root)
    return;
  // TODO
}

local LinkedList RTreeFindByArea(RTree *rTree, RTreeRect rect)
{

}

local LinkedList RTreeFindByPoint(RTree *rTree, int x, int y)
{

}

local Irtree rTreeInt = {
  INTERFACE_HEAD_INIT(I_BVH, "RTree")
  RTreeInit, RTreeDeinit, RTreeAdd, RTreeRemove, RTreeFindByArea, RTreeFindByPoint
};

EXPORT int MM_hs_rtree(int action, Imodman *mm_, Arena *arena)
{
  if (action == MM_LOAD)
  {
    mm = mm_;

    mm->RegInterface(&rTreeInt, ALLARENAS);
    return MM_OK;
  }
  else if (action == MM_UNLOAD)
  {
    if (mm->UnregInterface(&rTreeInt, ALLARENAS))
      return MM_FAIL;

    return MM_OK;
  }

  return MM_FAIL;
}