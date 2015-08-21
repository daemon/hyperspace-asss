#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "hs_rTree.h"

#define N_RTREE_ELEMENTS 4
#define RTREE_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define RTREE_MAX(a, b) (((a) > (b)) ? (a) : (b))

local Imodman *mm;

typedef struct RTreeNode
{
  RTreeRect bounds;
  void *data;
  RTreeNode *children[N_RTREE_ELEMENTS];
  size_t nChildren;
  RTreeNode *parent;
} RTreeNode;

// Prototypes
local void RTreeInit(RTree *rTree);
local void RTreeDeinit(RTree *rTree);
local void RTreeAdd(RTree *rTree, RTreeRect rect, void *data);
local void RTreeRemove(RTree *rTree, void *data);
local LinkedList RTreeFindByArea(RTree *rTree, RTreeRect rect);
local LinkedList RTreeFindByPoint(RTree *rTree, int x, int y);

local void freeRTreeNode(RTreeNode *rTreeNode);
local void rTreeNodeAdd(RTreeNode *rTreeNode, RTreeRect rect, void *data);
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

local RTreeNode *createRTreeNode(RTreeRect bounds, void *data, RTreeNode *parent, RTreeNode **children)
{
  RTreeNode *node = amalloc(sizeof(*node));
  node->bounds = rect;
  node->data = data;
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
  RTreeNode *worst[2] = {NULL, NULL};
  int sumAreas = 0;
  for (int i = 0; i < N_RTREE_ELEMENTS; ++i)
    for (int j = 0; j < N_RTREE_ELEMENTS; ++j)
    {
      if (i == j)
        continue;
      int ijArea = areaOf(minBB(node->children[i]->bounds, node->children[j]->bounds));
      if (ijArea >= sumAreas)
      {
        worst[0] = children[i];
        worst[1] = children[j];
      }
    }
  
  return worst;
}

RTreeNode *insertHeuristic(RTreeNode *first, RTreeNode *second, RTreeNode *node)
{
  int origArea1 = areaOf(first->bounds);
  int origArea2 = areaOf(second->bounds);  
  int area1 = areaOf(minBB(node->children[i]->bounds, first->bounds));
  int area2 = areaOf(minBB(node->children[i]->bounds, second->bounds));

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
  else if (worstPair[0]->nChildren > worstPair[1]->nChildren)
    return second;
  else
    return first;
}

local RTreeNode **splitRTreeNode(RTreeNode *node)
{
  RTreeNode *worstPair[2] = findWorstPair(node);
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

  return worstPair;    
}

local inline void rTreeNodeMove(RTreeNode *parent, RTreeNode *child);
{
  parent->bounds = minBB(parent->bounds, child->bounds);
  parent->children[parent->nChildren++] = child;
  child->parent = parent;
}

local void copyRTreeNode(RTreeNode *dest, RTreeNode *src)
{
  dest->nChildren = src->nChildren;
  dest->children = src->children;
  dest->data = src->data;
  dest->bounds = src->bounds;
  dest->leaf = src->leaf;
  dest->parent = src->parent;
}

local void rTreeNodeAdd(RTreeNode *rTreeNode, RTreeRect rect, void *data)
{
  if (!rTreeNode->leaf)
  {
    RTreeNode *minNode = NULL;
    int minExpansionArea = INT_MAX;
    for (int i = 0; i < N_RTREE_ELEMENTS; ++i)
    {
      if (!rTreeNode->children[i])
        break;

      int expansionArea = areaOf(minBB(rTreeNode->children[i]->bounds, rect)) - 
        areaOf(rTreeNode->children[i]->bounds);
      if (expansionArea < minExpansionArea)
      {
        minExpansionArea = expansionArea;
        minNode = rTreeNode->children[i];
      }
    }

    rTreeNodeAdd(minNode, rect, data);
  }
  else if (rTreeNode->nChildren < N_RTREE_ELEMENTS && rTreeNode->leaf)
  {
    rTreeNode->bounds = minBB(rTreeNode->bounds, rect);
    rTreeNode->children[rTreeNode->nChildren++] = createRTreeNode(rect, data, rTreeNode, true, NULL);
    rTreeNode->parent = rTreeNode;
    rTreeNodeFix(rTreeNode->parent, rTreeNode);
  }
  else 
  {
    RTreeNode **pair = splitRTreeNode(rTreeNode);
    RTreeNode *node = createRTreeNode(rect, data, rTreeNode, true, NULL);
    rTreeNodeMove(insertHeuristic(pair[0], pair[1], node), node);    

    afree(rTreeNode);
    // handle root node here, check if parent is null and create new level if necesary
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
    rTree->root = createRTreeNode(rect, data, NULL, NULL);
  } else {
    rTreeNodeAdd(rTree->root, rect, data);
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

local Istructman rTreeInt = {
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
    aman->FreeArenaData(adkey);
    pd->FreePlayerData(pdkey);

    releaseInterfaces();    
    if (mm->UnregInterface(&rTreeInt, ALLARENAS))
      return MM_FAIL;

    return MM_OK;
  }

  return MM_FAIL;
}