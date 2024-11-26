#include "spindle.h"

#include "utility.h"
#include "internal.h"

typedef struct {
  Vec(SB_Node*) packed;
  Vec(int) sparse;
  Vec(SB_Node*) stack;
} Worklist;

static void worklist_add(Worklist* wl, SB_Node* node) {
  while (node->id >= vec_len(wl->sparse)) {
    vec_put(wl->sparse, -1);
  }

  if (wl->sparse[node->id] == -1) {
    wl->sparse[node->id] = vec_len(wl->packed);
    vec_put(wl->packed, node);
  }
}

static void worklist_remove(Worklist* wl, SB_Node* node) {
  if (node->id >= vec_len(wl->sparse)) {
    return;
  }

  int index = wl->sparse[node->id];

  if (index == -1) {
    return;
  }

  SB_Node* last = wl->packed[index] = vec_pop(wl->packed);

  wl->sparse[last->id] = index;
  wl->sparse[node->id] = -1;
}

static SB_Node* worklist_pop(Worklist* wl) {
  SB_Node* node = vec_pop(wl->packed);
  wl->sparse[node->id] = -1;
  return node;
}

static bool worklist_empty(Worklist* wl) {
  return vec_len(wl->packed) == 0;
}

static void remove_use(SB_Node* node, SB_Node* user, int index) {
  for (SB_Use** pu = &node->uses; *pu;) {
    SB_Use* u = *pu;

    if (u->node == user && u->index == index) {
      *pu = u->next;
      return;
    }
    else {
      pu = &u->next;
    }
  }

  assert(false);
}

static void remove_node(Worklist* wl, SB_Node* first) {
  vec_clear(wl->stack);
  vec_put(wl->stack, first);

  while (vec_len(wl->stack)) {
    SB_Node* node = vec_pop(wl->stack);
    assert(!node->uses);

    worklist_remove(wl, node);

    for (int32_t i = 0; i < node->num_ins; ++i) {
      if (!node->ins[i]) {
        continue;
      }

      remove_use(node->ins[i], node, i);

      if (!node->ins[i]->uses) {
        vec_put(wl->stack, node->ins[i]);
      }
    }
  }
}

static void push_uses(Worklist* wl, SB_Node* node) {
  for (SB_Use* use = node->uses; use; use = use->next) {
    worklist_add(wl, use->node);
  }
}

static void replace_node(Worklist* wl, SB_Node* target, SB_Node* source) {
  assert(target != source);

  push_uses(wl, target);

  for (SB_Use* use = target->uses; use; use = use->next) {
    assert(use->node->ins[use->index] == target);
    use->node->ins[use->index] = source;
  }

  SB_Use** tail = &source->uses;
  while (*tail) {
    tail = &(*tail)->next;
  }

  *tail = target->uses;
  target->uses = NULL;

  remove_node(wl, target);
}

typedef struct {
  Worklist* wl;
} IdealizeContext;

typedef SB_Node*(*IdealizeFunc)(IdealizeContext*, SB_Node*);

static SB_Node* idealize_phi(IdealizeContext* ctx, SB_Node* node) {
  SB_Node* same = NULL;

  for (int32_t i = 1; i < node->num_ins; ++i) {
    if (!node->ins[i]) {
      continue;
    }

    if (!same) {
      same = node->ins[i];
    }

    if (same != node->ins[i]) {
      return node;
    }
  }

  if (!same) {
    return node;
  }

  worklist_add(ctx->wl, node->ins[0]);

  return same;
}

static SB_Node* idealize_region(IdealizeContext* ctx, SB_Node* node) {
  (void)ctx;

  for (SB_Use* use = node->uses; use; use = use->next) {
    if (use->node->kind == SB_NODE_PHI) {
      return node;
    }
  }

  SB_Node* same = NULL;

  for (int32_t i = 0; i < node->num_ins; ++i) {
    if (!node->ins[i]) {
      continue;
    }

    if (!same) {
      same = node->ins[i];
    }

    if (same != node->ins[i]) {
      return node;
    }
  } 

  assert(same);
  return same;
}

static SB_Node* idealize_load(IdealizeContext* ctx, SB_Node* node) {
  (void)ctx;

  SB_Node* mem = node->ins[1];

  if (mem->kind == SB_NODE_STORE && mem->ins[2] == node->ins[2]) {
    return mem->ins[3];
  }

  return node;
}

static IdealizeFunc idealize_table[NUM_SB_NODE_KINDS] = {
  [SB_NODE_PHI] = idealize_phi,
  [SB_NODE_REGION] = idealize_region,
  [SB_NODE_LOAD] = idealize_load,
};

static void peeps(Worklist* wl) {
  IdealizeContext ideal_ctx = {
    .wl = wl
  };

  while (!worklist_empty(wl)) {
    SB_Node* node = worklist_pop(wl);

    IdealizeFunc idealize = idealize_table[node->kind];

    if (idealize) {
      SB_Node* ideal = idealize(&ideal_ctx, node);

      if (ideal != node) {
        replace_node(wl, node, ideal);
      }
    }
  }
}

typedef enum {
  DSE_NO_READS,
  DSE_READS
} DSE_State;

static void dead_store_elim(SB_Func* func, Worklist* wl) {
  Scratch scratch = scratch_get(0, NULL);

  DSE_State* states = arena_array(scratch.arena, DSE_State, func->next_id);

  vec_clear(wl->stack);

  GraphWalk walk = post_order_walk_ins(scratch.arena, func);

  size_t num_stores = 0;
  SB_Node** stores = arena_array(scratch.arena, SB_Node*, walk.count);

  for (size_t i = 0; i < walk.count; ++i) {
    SB_Node* node = walk.nodes[i];

    if (node->flags & SB_FLAG_READS_MEM) {
      assert(node->flags & SB_FLAG_HAS_MEM_DEP);
      vec_put(wl->stack, node);
    }

    if (node->kind == SB_NODE_STORE) {
      stores[num_stores++] = node;
    }
  }

  while (vec_len(wl->stack)) {
    SB_Node* node = vec_pop(wl->stack);

    if (states[node->id] == DSE_READS) {
      continue;
    }

    states[node->id] = DSE_READS;

    if (node->kind == SB_NODE_PHI) {
      for (int32_t i = 1; i < node->num_ins; ++i) {
        if (!node->ins[i]) {
          continue;
        }

        vec_put(wl->stack, node->ins[i]);
      }
    }
    else if (node->flags & SB_FLAG_HAS_MEM_DEP) {
      vec_put(wl->stack, node->ins[1]);
    }
  }

  for (size_t i = 0; i < num_stores; ++i) {
    SB_Node* store = stores[i];

    if (states[store->id] == DSE_READS) {
      continue;
    }

    replace_node(wl, store, store->ins[1]);
  }

  scratch_release(&scratch);
}

void sb_opt(SB_Context* ctx, SB_Func* func) {
  (void)ctx;

  Scratch scratch = scratch_get(0, NULL);

  Worklist wl = {0};

  GraphWalk walk = post_order_walk_ins(scratch.arena, func);

  for (size_t i = 0; i < walk.count; ++i) {
    worklist_add(&wl, walk.nodes[i]);
  }

  while (true) {
    dead_store_elim(func, &wl);

    if (!worklist_empty(&wl)) {
      peeps(&wl);
    }
    else {
      break;
    }
  }

  vec_free(wl.packed);
  vec_free(wl.sparse);
  vec_free(wl.stack);

  scratch_release(&scratch);
}