#include <stdio.h>

#include "spindle.h"
#include "utility.h"
#include "internal.h"

#define DATA(node, ty) ((ty*)(node_data_raw(node)))

struct SB_Context {
  Arena* arena;
};

typedef struct {
  uint64_t value;
} ConstantData;

static void* node_data_raw(SB_Node* node) {
  return ptr_byte_add(node, sizeof(SB_Node));
}

SB_Context* sb_init() {
  Arena* arena = new_arena();

  SB_Context* ctx = arena_type(arena, SB_Context);
  ctx->arena = arena;

  return ctx;
}

void sb_cleanup(SB_Context* ctx) {
  free_arena(ctx->arena);
}

SB_Func* sb_begin_func(SB_Context* ctx) {
  SB_Func* func = arena_type(ctx->arena, SB_Func);
  func->context = ctx;
  func->next_id = 1;

  return func;
}

typedef struct {
  bool children_processed;
  SB_Node* node;
} PostOrderNode;

static PostOrderNode post_order_node(bool children_processed, SB_Node* node) {
  return (PostOrderNode) {
    .children_processed = children_processed,
    .node = node
  };
}

GraphWalk post_order_walk_ins(Arena* arena, SB_Func* func) {
  size_t count = 0;
  SB_Node** nodes = arena_array(arena, SB_Node*, func->next_id);

  uint64_t* visited = arena_array(arena, uint64_t, bitset_num_u64(func->next_id));

  Vec(PostOrderNode) stack = NULL;
  vec_put(stack, post_order_node(false, func->end));

  while (vec_len(stack)) {
    PostOrderNode n = vec_pop(stack);

    if (!n.children_processed) {
      if (bitset_get(visited, n.node->id)) {
        continue;
      }

      bitset_set(visited, n.node->id);

      vec_put(stack, post_order_node(true, n.node));

      for (int32_t i = 0; i < n.node->num_ins; ++i) {
        if (n.node->ins[i]) {
          vec_put(stack, post_order_node(false, n.node->ins[i]));
        }
      }
    }
    else {
      nodes[count++] = n.node;
    }
  }

  vec_free(stack);

  return (GraphWalk) {
    .count = count,
    .nodes = nodes,
    .visited = visited
  };
}

void sb_finish_func(SB_Func* func) {
  Scratch scratch = scratch_get(0, NULL);

  assert(func->start);
  assert(func->end);

  GraphWalk walk = post_order_walk_ins(scratch.arena, func);

  assert(bitset_get(walk.visited, func->start->id) && "function never terminates");

  for (size_t i = 0; i < walk.count; ++i) {
    SB_Node* node = walk.nodes[i];

    for (SB_Use** use = &node->uses; *use;) {
      if (!bitset_get(walk.visited, (*use)->node->id)) {
        *use = (*use)->next;
      }
      else {
        use = &(*use)->next;
      }
    }
  }

  scratch_release(&scratch);
}

static const char* gv_label(SB_Node* node, char* buf, size_t buf_cap) {
  switch (node->kind) {
    default:
      return sb_node_kind_label[node->kind];
    case SB_NODE_CONSTANT: {
      uint64_t value = DATA(node, ConstantData)->value;
      snprintf(buf, buf_cap, "%lld", value);
      return buf;
    } break;
  }
}

void sb_graphviz_func(FILE* stream, SB_Func* func) {
  Scratch scratch = scratch_get(0, NULL);

  GraphWalk walk = post_order_walk_ins(scratch.arena, func);

  fprintf(stream, "digraph G {\n");
  fprintf(stream, "  rankdir=BT;\n");

  fprintf(stream, "  subgraph cluster {\n");

  for (size_t i = 0; i < walk.count; ++i) {
    SB_Node* node = walk.nodes[i];

    if (node->flags & SB_FLAG_IS_PROJ) {
      continue;
    }

    bool has_proj = false;

    for (SB_Use* use = node->uses; !has_proj && use; use = use->next) {
      if (use->node->flags & SB_FLAG_IS_PROJ) {
        has_proj = true;
      }
    }

    fprintf(stream, "    n%d [", node->id);

    char buf[512];
    const char* label = gv_label(node, buf, ARRAY_LENGTH(buf));

    if (!has_proj) {
      if (node->flags & SB_FLAG_IS_CFG) {
        fprintf(stream, "style=filled,fillcolor=yellow,");
      }

      fprintf(stream, "label=\"%s\"", label);
    }
    else {
      fprintf(stream, "shape=plaintext, label=<<table border=\"0\" cellborder=\"1\" cellspacing=\"0\" cellpadding=\"4\">");

      fprintf(stream, "<tr><td%s>", (node->flags & SB_FLAG_IS_CFG) ? " bgcolor=\"yellow\"" : "");
      fprintf(stream, "%s</td></tr>", label);

      fprintf(stream, "<tr><td>");
      fprintf(stream, "<table border=\"0\" cellborder=\"1\" cellspacing=\"0\" cellpadding=\"4\">");
      fprintf(stream, "<tr>");

      for (SB_Use* use = node->uses; use; use = use->next) {
        if (!(use->node->flags & SB_FLAG_IS_PROJ)) {
          continue;
        }

        fprintf(stream, "<td%s", (use->node->flags & SB_FLAG_IS_CFG) ? " bgcolor=\"yellow\"" : "");

        fprintf(stream, " port=\"p%s\">%s</td>", sb_node_kind_label[use->node->kind], sb_node_kind_label[use->node->kind]);
      }

      fprintf(stream, "</tr>");
      fprintf(stream, "</table>");
      fprintf(stream, "</td></tr>");

      fprintf(stream, "</table>>");
    }

    fprintf(stream, "];\n"); 

    for (int32_t j = 0; j < node->num_ins; ++j) {
      if (!node->ins[j]) {
        continue;
      }

      if (node->ins[j]->kind == SB_NODE_START && !(node->flags & SB_FLAG_IS_PROJ)) {
        continue;
      }

      fprintf(stream, "    n%d -> ", node->id);

      if (node->ins[j]->flags & SB_FLAG_IS_PROJ) {
        fprintf(stream, "n%d:p%s", node->ins[j]->ins[0]->id, sb_node_kind_label[node->ins[j]->kind]);
      }
      else {
        fprintf(stream, "n%d", node->ins[j]->id);
      }

      fprintf(stream, "[taillabel=\"%d\"];\n", j);
    }
  }

  fprintf(stream, "  }\n");
  fprintf(stream, "}\n\n");

  scratch_release(&scratch);
}

static void init_ins(SB_Func* func, SB_Node* node, int32_t num_ins) {
  assert(node->ins == NULL);
  node->num_ins = num_ins;
  node->ins = arena_array(func->context->arena, SB_Node*, num_ins);
}

static SB_Node* new_node_with_data(SB_Func* func, SB_NodeKind kind, int32_t num_ins, size_t data_size) {
  assert(kind);
  assert(kind == SB_NODE_START || kind == SB_NODE_PHI || kind == SB_NODE_REGION || num_ins != 0);

  SB_Node* node = arena_zeroed(func->context->arena, sizeof(SB_Node) + data_size);

  node->id = func->next_id++;
  node->kind = kind;
  init_ins(func, node, num_ins);

  return node;
}

static SB_Node* new_node(SB_Func* func, SB_NodeKind kind, int32_t num_ins)  {
  return new_node_with_data(func, kind, num_ins, 0);
}


static void set_input(SB_Func* func, SB_Node* node, int32_t index, SB_Node* input) {
  assert(input);
  assert(!node->ins[index]);

  assert(index < node->num_ins);

  node->ins[index] = input;

  SB_Use* use = arena_type(func->context->arena, SB_Use);
  use->node = node;
  use->index = index;

  use->next = input->uses;
  input->uses = use;
}

static SB_Node* new_leaf(SB_Func* func, SB_NodeKind kind, size_t data_size) {
  assert(func->start);
  SB_Node* node = new_node_with_data(func, kind, 1, data_size);
  set_input(func, node, 0, func->start);
  return node;
}

static SB_Node* new_proj(SB_Func* func, SB_NodeKind kind, SB_Node* parent) {
  SB_Node* node = new_node(func, kind, 1);
  node->flags |= SB_FLAG_IS_PROJ;
  set_input(func, node, 0, parent);
  return node;
}

SB_Node* sb_node_start(SB_Func* func) {
  assert(!func->start);
  func->start = new_node(func, SB_NODE_START, 0);
  func->start->flags |= SB_FLAG_IS_CFG;
  return func->start;
}

SB_Node* sb_node_start_ctrl(SB_Func* func, SB_Node* start) {
  assert(start->kind == SB_NODE_START);
  SB_Node* node = new_proj(func, SB_NODE_START_CTRL, start);
  node->flags |= SB_FLAG_IS_CFG;
  return node;
}

SB_Node* sb_node_start_mem(SB_Func* func, SB_Node* start) {
  assert(start->kind == SB_NODE_START);
  SB_Node* node = new_proj(func, SB_NODE_START_MEM, start);
  return node;
}

SB_Node* sb_node_end(SB_Func* func, SB_Node* ctrl, SB_Node* mem, SB_Node* return_value) {
  assert(!func->end);

  SB_Node* node = new_node(func, SB_NODE_END, 3);
  set_input(func, node, 0, ctrl);
  set_input(func, node, 1, mem);
  set_input(func, node, 2, return_value);

  node->flags |= SB_FLAG_IS_CFG;

  func->end = node;

  return node;
}

SB_Node* sb_node_null(SB_Func* func) {
  return new_leaf(func, SB_NODE_NULL, 0);
}

SB_Node* sb_node_region(SB_Func* func) {
  SB_Node* node = new_node(func, SB_NODE_REGION, 0);
  node->flags |= SB_FLAG_IS_CFG;
  return node;
}

void sb_set_region_ins(SB_Func* func, SB_Node* region, int32_t num_ins, SB_Node** ins) {
  assert(num_ins);
  assert(region->kind == SB_NODE_REGION);

  init_ins(func, region, num_ins);

  for (int32_t i = 0; i < num_ins; ++i) {
    set_input(func, region, i, ins[i]);
  }
}

SB_Node* sb_node_phi(SB_Func* func) {
  return new_node(func, SB_NODE_PHI, 0);
}

void sb_set_phi_ins(SB_Func* func, SB_Node* phi, SB_Node* region, int32_t num_ins, SB_Node** ins) {
  assert(phi->kind == SB_NODE_PHI);
  assert(region->kind == SB_NODE_REGION);
  assert(num_ins == region->num_ins);

  init_ins(func, phi, num_ins + 1);
  set_input(func, phi, 0, region);

  for (int32_t i = 0; i < num_ins; ++i) {
    set_input(func, phi, i + 1, ins[i]);
  }
}

SB_Node* sb_node_branch(SB_Func* func, SB_Node* ctrl, SB_Node* predicate) {
  SB_Node* branch = new_node(func, SB_NODE_BRANCH, 2);

  set_input(func, branch, 0, ctrl);
  set_input(func, branch, 1, predicate);

  branch->flags = SB_FLAG_IS_CFG;

  return branch;
}

SB_Node* sb_node_branch_true(SB_Func* func, SB_Node* branch) {
  assert(branch->kind == SB_NODE_BRANCH);
  SB_Node* node = new_proj(func, SB_NODE_BRANCH_TRUE, branch);
  node->flags |= SB_FLAG_IS_CFG;
  return node;
}

SB_Node* sb_node_branch_false(SB_Func* func, SB_Node* branch) {
  assert(branch->kind == SB_NODE_BRANCH);
  SB_Node* node = new_proj(func, SB_NODE_BRANCH_FALSE, branch);
  node->flags |= SB_FLAG_IS_CFG;
  return node;
}

SB_Node* sb_node_store(SB_Func* func, SB_Node* ctrl, SB_Node* mem, SB_Node* address, SB_Node* value) {
  SB_Node* node = new_node(func, SB_NODE_STORE, 4);
  set_input(func, node, 0, ctrl);
  set_input(func, node, 1, mem);
  set_input(func, node, 2, address);
  set_input(func, node, 3, value);
  node->flags |= SB_FLAG_HAS_MEM_DEP;
  return node;
}

SB_Node* sb_node_load(SB_Func* func, SB_Node* ctrl, SB_Node* mem, SB_Node* address) {
  SB_Node* node = new_node(func, SB_NODE_LOAD, 3);
  set_input(func, node, 0, ctrl);
  set_input(func, node, 1, mem);
  set_input(func, node, 2, address);
  node->flags |= SB_FLAG_READS_MEM | SB_FLAG_HAS_MEM_DEP;
  return node;
}

SB_Node* sb_node_mem_escape(SB_Func* func, SB_Node* mem) {
  SB_Node* node = new_node(func, SB_NODE_MEM_ESCAPE, 2);
  set_input(func, node, 1, mem);
  node->flags |= SB_FLAG_READS_MEM | SB_FLAG_HAS_MEM_DEP;
  return node;
}

SB_Node* sb_node_alloca(SB_Func* func) {
  return new_leaf(func, SB_NODE_ALLOCA, 0);
}

SB_Node* sb_node_constant(SB_Func* func, uint64_t value) {
  SB_Node* node = new_leaf(func, SB_NODE_CONSTANT, sizeof(ConstantData));
  DATA(node, ConstantData)->value = value;
  return node;
}

static SB_Node* new_binary_node(SB_Func* func, SB_NodeKind kind, SB_Node* lhs, SB_Node* rhs) {
  SB_Node* node = new_node(func, kind, 2);
  set_input(func, node, 0, lhs);
  set_input(func, node, 1, rhs);
  return node;
}

SB_Node* sb_node_add(SB_Func* func, SB_Node* lhs, SB_Node* rhs) {
  return new_binary_node(func, SB_NODE_ADD, lhs, rhs);
}

SB_Node* sb_node_sub(SB_Func* func, SB_Node* lhs, SB_Node* rhs) {
  return new_binary_node(func, SB_NODE_SUB, lhs, rhs);
}

SB_Node* sb_node_mul(SB_Func* func, SB_Node* lhs, SB_Node* rhs) {
  return new_binary_node(func, SB_NODE_MUL, lhs, rhs);
}

SB_Node* sb_node_sdiv(SB_Func* func, SB_Node* lhs, SB_Node* rhs) {
  return new_binary_node(func, SB_NODE_SDIV, lhs, rhs);
}