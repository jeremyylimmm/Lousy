#pragma once

#include <stdint.h>
#include <stdio.h>

#define X(name, ...) SB_NODE_##name,
typedef enum {
  SB_NODE_UNINITIALIZED,
  #include "node_kind.def" 
  NUM_SB_NODE_KINDS
} SB_NodeKind;
#undef X

#define X(name, label, ...) label,
static char* sb_node_kind_label[] = {
  "!!uninitialized!!",
  #include "node_kind.def"
};
#undef X

typedef struct SB_Use  SB_Use;
typedef struct SB_Node SB_Node;

#define SB_BIT(x) (1 << x)

typedef enum {
  SB_FLAG_NONE = 0,
  SB_FLAG_IS_PROJ = SB_BIT(0),
  SB_FLAG_IS_CFG = SB_BIT(1),
  SB_FLAG_READS_MEM = SB_BIT(2),
  SB_FLAG_HAS_MEM_DEP = SB_BIT(3),
} SB_Flags;

struct SB_Use {
  SB_Use* next;

  int32_t index;
  SB_Node* node;
};

struct SB_Node {
  int32_t id;
  SB_Flags flags;

  SB_NodeKind kind;

  int32_t num_ins;
  SB_Node** ins;

  SB_Use* uses;
};

typedef struct SB_Context SB_Context;

typedef struct {
  SB_Context* context;
  int32_t next_id;

  SB_Node* start;
  SB_Node* end;
} SB_Func;

SB_Context* sb_init();
void sb_cleanup(SB_Context* ctx);

SB_Func* sb_begin_func(SB_Context* ctx);
void sb_finish_func(SB_Func* func);

void sb_graphviz_func(FILE* stream, SB_Func* func);

SB_Node* sb_node_start(SB_Func* func);
SB_Node* sb_node_start_ctrl(SB_Func* func, SB_Node* start);
SB_Node* sb_node_start_mem(SB_Func* func, SB_Node* start);

SB_Node* sb_node_end(SB_Func* func, SB_Node* ctrl, SB_Node* mem, SB_Node* return_value);

SB_Node* sb_node_null(SB_Func* func);

SB_Node* sb_node_region(SB_Func* func);
void sb_set_region_ins(SB_Func* func, SB_Node* region, int32_t num_ins, SB_Node** ins);

SB_Node* sb_node_phi(SB_Func* func);
void sb_set_phi_ins(SB_Func* func, SB_Node* phi, SB_Node* region, int32_t num_ins, SB_Node** ins);

SB_Node* sb_node_branch(SB_Func* func, SB_Node* ctrl, SB_Node* predicate);
SB_Node* sb_node_branch_true(SB_Func* func, SB_Node* branch);
SB_Node* sb_node_branch_false(SB_Func* func, SB_Node* branch);

SB_Node* sb_node_store(SB_Func* func, SB_Node* ctrl, SB_Node* mem, SB_Node* address, SB_Node* value);
SB_Node* sb_node_load(SB_Func* func, SB_Node* ctrl, SB_Node* mem, SB_Node* address);

SB_Node* sb_node_mem_escape(SB_Func* func, SB_Node* mem);

SB_Node* sb_node_alloca(SB_Func* func);

SB_Node* sb_node_constant(SB_Func* func, uint64_t value);

SB_Node* sb_node_add(SB_Func* func, SB_Node* lhs, SB_Node* rhs);
SB_Node* sb_node_sub(SB_Func* func, SB_Node* lhs, SB_Node* rhs);
SB_Node* sb_node_mul(SB_Func* func, SB_Node* lhs, SB_Node* rhs);
SB_Node* sb_node_sdiv(SB_Func* func, SB_Node* lhs, SB_Node* rhs);

void sb_opt(SB_Context* ctx, SB_Func* func);