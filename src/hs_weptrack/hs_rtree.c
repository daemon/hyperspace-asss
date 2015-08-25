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
local void freeRTreeNode(RTreeNode *rTreeNode, bool freeData);
local RTreeNode *createRTreeNode(RTreeRect bounds, void *data, RTreeNode *parent, bool leaf, RTreeNode **children, size_t nChildren);
local inline int areaOf(RTreeRect bounds);
local inline RTreeRect minBB(RTreeRect a, RTreeRect b);
local RTreeNode **findWorstPair(RTreeNode *node);
local RTreeNode **splitRTreeNode(RTreeNode *node);
local inline void rTreeNodeMove(RTreeNode *parent, RTreeNode *child);
local void rTreeNodeFitExpand(RTreeNode *parent, RTreeNode *child);
local void rTreeNodeRemoveChild(RTreeNode *rTreeNode, RTreeNode *child);
local int nLeaves(RTreeNode *node);;
local void rTreeNodeAdd(RTree *rTree, RTreeNode *rTreeNode, RTreeNode *child, bool up);
local void RTreeFree(RTree *rTree);
local void RTreeInit(RTree *rTree);
local void RTreeDeinit(RTree *rTree);
local void RTreeAdd(RTree *rTree, RTreeRect rect, void *data);
local void rTreeNodeFitShrink(RTreeNode *node);
local void checkRTreeNodeRemoval(RTree *rTree, RTreeNode *node);
local void rTreeNodeRemove(RTree *rTree, RTreeNode *node, void *data);
local void RTreeRemove(RTree *rTree, void *data);
local LinkedList RTreeFindByRect(RTree *rTree, RTreeRect rect);
local LinkedList RTreeFindByPoint(RTree *rTree, int x, int y);
local int nLeaves(RTreeNode *node);
local inline bool intersects(RTreeRect rect1, RTreeRect rect2);

local inline bool intersects(RTreeRect rect1, RTreeRect rect2)
{
  return !(rect1.y2 < rect2.y1 || rect1.x2 < rect2.x1 || 
  rect1.x1 > rect2.x2 || rect1.y1 > rect2.x2);
}

local void freeRTreeNode(RTreeNode *rTreeNode, bool freeData)
{
  for (int i = 0; i < N_RTREE_ELEMENTS; ++i)
    if (rTreeNode->children[i])
      freeRTreeNode(rTreeNode->children[i], freeData);
  
  if (freeData && rTreeNode->data)
    afree(rTreeNode->data);
  return;
}

local RTreeNode *createRTreeNode(RTreeRect bounds, void *data, RTreeNode *parent, bool leaf, RTreeNode **children, size_t nChildren)
{
  RTreeNode *node = amalloc(sizeof(*node));
  node->bounds = bounds;
  node->data = data;
  node->leaf = leaf;
  node->parent = parent;

  for (int i = 0; i < nChildren; ++i)
  {
    node->children[i] = children[i];
    node->children[i]->parent = node;
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
    createRTreeNode(worstPair[0]->bounds, worstPair[0]->data, NULL, worstPair[0]->leaf, worstPair[0]->children, worstPair[0]->nChildren),
    createRTreeNode(worstPair[1]->bounds, worstPair[1]->data, NULL, worstPair[1]->leaf, worstPair[1]->children, worstPair[1]->nChildren)
  };

  worstPair[0]->nChildren = 0;
  worstPair[1]->nChildren = 0;

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

local void rTreeNodeFitExpand(RTreeNode *parent, RTreeNode *child)
{
  if (!parent)
    return;
  parent->bounds = minBB(child->bounds, parent->bounds);
  rTreeNodeFitExpand(parent->parent, parent);
}

local void rTreeNodeRemoveChild(RTreeNode *rTreeNode, RTreeNode *child)
{
  int removeIndex = 0;
  for (size_t i = 0; i < N_RTREE_ELEMENTS; ++i)
    if (rTreeNode->children[i] == child)
    {
      removeIndex = i;
      rTreeNode->children[i] = NULL;
      break;
    }

  if (removeIndex == N_RTREE_ELEMENTS - 1)
    return;

  memcpy(rTreeNode->children + removeIndex, 
    rTreeNode->children + removeIndex + 1, 
    (N_RTREE_ELEMENTS - removeIndex - 1) * sizeof(RTreeNode *));
  --rTreeNode->nChildren;
}

local int nLeaves(RTreeNode *node);

local void rTreeNodeAdd(RTree *rTree, RTreeNode *rTreeNode, RTreeNode *child, bool up) //RTreeRect rect, void *data)
{
  if (!rTreeNode->leaf && !up)
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

    rTreeNodeAdd(rTree, minNode, child, false);
  } else if (rTreeNode->leaf && !up) {
    rTreeNodeAdd(rTree, rTreeNode->parent, child, true);
  } else if (rTreeNode->nChildren < N_RTREE_ELEMENTS && up) {
    rTreeNode->bounds = minBB(rTreeNode->bounds, child->bounds);
    rTreeNode->children[rTreeNode->nChildren++] = child;
    child->parent = rTreeNode;
    rTreeNodeFitExpand(rTreeNode->parent, rTreeNode);
  } else {
    RTreeNode **pair = splitRTreeNode(rTreeNode);
    // RTreeNode *node = createRTreeNode(rect, data, rTreeNode, true, NULL);
    rTreeNodeMove(insertHeuristic(pair[0], pair[1], child), child);    

    if (rTreeNode->parent)
    {
      rTreeNodeRemoveChild(rTreeNode->parent, rTreeNode);
      rTreeNodeAdd(rTree, rTreeNode->parent, pair[0], true);
      rTreeNodeAdd(rTree, rTreeNode->parent, pair[1], true);
    } else {
      RTreeNode *children[2] = {pair[0], pair[1]};
      RTreeNode *node = createRTreeNode(minBB(pair[0]->bounds, pair[1]->bounds), NULL, NULL, false, children, 2);
      rTree->root = node;
    }

    afree(pair);
    afree(rTreeNode);
  }
}

local void RTreeFree(RTree *rTree)
{
  if (rTree->root)
    freeRTreeNode(rTree->root, true);
  rTree->root = NULL;
  afree(rTree);
}

local void RTreeInit(RTree *rTree)
{
  rTree->root = NULL;
}

local void RTreeDeinit(RTree *rTree)
{
  if (rTree->root)
    freeRTreeNode(rTree->root, false);
  rTree->root = NULL;
}

local void RTreeAdd(RTree *rTree, RTreeRect rect, void *data)
{
  RTreeNode *node = createRTreeNode(rect, data, NULL, true, NULL, 0);
  if (!rTree->root)
  {
    rTree->root = createRTreeNode(rect, NULL, NULL, false, NULL, 0);
    rTree->root->children[0] = node;
    ++rTree->root->nChildren;
    node->parent = rTree->root;
  } else 
    rTreeNodeAdd(rTree, rTree->root, node, false);
}

local void rTreeNodeFitShrink(RTreeNode *node)
{
  // TODO for remove  
}

local void checkRTreeNodeRemoval(RTree *rTree, RTreeNode *node)
{
  if (node->nChildren != 0)
    return;
  
  if (node->parent)
  {
    rTreeNodeRemoveChild(node->parent, node);
    checkRTreeNodeRemoval(rTree, node->parent);
  } else
    rTree->root = NULL;
  
  afree(node);  
}

// TODO implement correctly later
local void rTreeNodeRemove(RTree *rTree, RTreeNode *node, void *data)
{
  if (!node)
    return;

  for (size_t i = 0; i < node->nChildren; ++i)
    if (!node->children[i]->leaf)
      rTreeNodeRemove(rTree, node->children[i], data);
    else if (node->children[i]->data == data)
    {
      rTreeNodeRemoveChild(node, node->children[i]);
      afree(node->children[i]);

      checkRTreeNodeRemoval(rTree, node);
      return;
    }    
}

local void RTreeRemove(RTree *rTree, void *data)
{
  if (!rTree->root)
    return;
  rTreeNodeRemove(rTree, rTree->root, data);
}

local void RTreeNodeFindByArea(RTreeNode *node, RTreeRect rect, LinkedList *ll)
{
  if (intersects(node->bounds, rect))
    if (node->leaf)
      LLAdd(ll, node->data);
    else
      for (size_t i = 0; i < node->nChildren; ++i)
        RTreeNodeFindByArea(node->children[i], rect, ll);
}

local LinkedList RTreeFindByRect(RTree *rTree, RTreeRect rect)
{
  LinkedList nodes = LL_INITIALIZER;
  RTreeNodeFindByArea(rTree->root, rect, &nodes);
  return nodes;
}

local LinkedList RTreeFindByPoint(RTree *rTree, int x, int y)
{
  RTreeRect rect = {x, y, x, y};
  return RTreeFindByRect(rTree, rect);
}

local Irtree rTreeInt = {
  INTERFACE_HEAD_INIT(I_RTREE, "rtree")
  RTreeInit, RTreeDeinit, RTreeFree, RTreeAdd, RTreeRemove, RTreeFindByRect, RTreeFindByPoint
};

local int nLeaves(RTreeNode *node)
{
  if (node->leaf)
    return 1;

  int sum = 0;
  for (size_t i = 0; i < node->nChildren; ++i)
    sum += nLeaves(node->children[i]);
  return sum;
}

EXPORT int MM_hs_rtree(int action, Imodman *mm_, Arena *arena)
{
  if (action == MM_LOAD)
  {
    mm = mm_;

    mm->RegInterface(&rTreeInt, ALLARENAS);
    /*RTree rtree;
    RTreeInit(&rtree);
    RTreeRect rect1 = {   0,  0, 24, 24 };
    RTreeRect rect2 = {   0,  0, 14, 34 };
    RTreeRect rect3 = { -10, -5, 11, 11 };
    
    for (int i = 0; i < 10; ++i)
    {
      int *tmp = amalloc(sizeof(*tmp));
      *tmp = i * 3 + 1;
      RTreeAdd(&rtree, rect1, tmp);      
      printf("%d\n\n", nLeaves(rtree.root));

      tmp = amalloc(*tmp);
      *tmp = i * 3 + 2;
      RTreeAdd(&rtree, rect2, tmp);
      printf("%d\n\n", nLeaves(rtree.root));

      tmp = amalloc(*tmp);
      *tmp = i * 3 + 3;
      RTreeAdd(&rtree, rect3, tmp);
      printf("%d\n\n", nLeaves(rtree.root));
    }
    RTreeRect rect = { 15, 15, 25, 25 };
    LinkedList ll = RTreeFindByRect(&rtree, rect);*/
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
