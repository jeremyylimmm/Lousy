#pragma once

#include "utility.h"
#include "spindle.h"

typedef struct {
  size_t count;
  SB_Node** nodes;
  uint64_t* visited;
} GraphWalk;

GraphWalk post_order_walk_ins(Arena* arena, SB_Func* func);