#include "front.h"

typedef struct {
  SB_Node* region;
  SB_Node* mem_phi;

  Vec(SB_Node*) ctrl_in;
  Vec(SB_Node*) mem_in;
} BlockData;

typedef struct {
  BlockData* block_data_map;

  SB_Func* func;
  SB_Node* ctrl;
  SB_Node* mem;
  SB_Node** places;

  Vec(SB_Node*)* v_end_ctrl;
  Vec(SB_Node*)* v_end_mem;
  Vec(SB_Node*)* v_end_val;

  bool had_return;
} LowerCtx;

#define IN(idx) sb_node_load(ctx->func, ctx->ctrl, ctx->mem, ctx->places[inst->reads[idx]])

static SB_Node* lower_INTEGER_CONST(LowerCtx* ctx, SemInst* inst) {
  return sb_node_constant(ctx->func, (uint64_t)inst->data);
}

static SB_Node* lower_ADD(LowerCtx* ctx, SemInst* inst) {
  return sb_node_add(ctx->func, IN(0), IN(1));
}

static SB_Node* lower_SUB(LowerCtx* ctx, SemInst* inst) {
  return sb_node_sub(ctx->func, IN(0), IN(1));
}

static SB_Node* lower_MUL(LowerCtx* ctx, SemInst* inst) {
  return sb_node_mul(ctx->func, IN(0), IN(1));
}
static SB_Node* lower_DIV(LowerCtx* ctx, SemInst* inst) {
  return sb_node_sdiv(ctx->func, IN(0), IN(1));
}

static SB_Node* lower_COPY(LowerCtx* ctx, SemInst* inst) {
  return IN(0);
}

static void push_func_exit(LowerCtx* ctx, SB_Node* ctrl, SB_Node* mem, SB_Node* ret_val) {
  vec_put(*ctx->v_end_ctrl, ctrl);
  vec_put(*ctx->v_end_mem, mem);
  vec_put(*ctx->v_end_val, ret_val);
}

static SB_Node* lower_RETURN(LowerCtx* ctx, SemInst* inst) {
  SB_Node* ret_val = inst->num_reads == 0 ? sb_node_null(ctx->func) : IN(0);
  ctx->had_return = true;
  push_func_exit(ctx, ctx->ctrl, ctx->mem, ret_val);
  return NULL;
}

static void push_block_entry(BlockData* data, SB_Node* ctrl, SB_Node* mem) {
  vec_put(data->ctrl_in, ctrl);
  vec_put(data->mem_in, mem);
}

static SB_Node* lower_GOTO(LowerCtx* ctx, SemInst* inst) {
  SemBlock* block = inst->data;
  BlockData* block_data = &ctx->block_data_map[block->_id];

  push_block_entry(block_data, ctx->ctrl, ctx->mem);

  return NULL;
}

static SB_Node* lower_BRANCH(LowerCtx* ctx, SemInst* inst) {
  SemBlock** locs = inst->data;

  BlockData* block_true = &ctx->block_data_map[locs[0]->_id];
  BlockData* block_false = &ctx->block_data_map[locs[1]->_id];

  SB_Node* branch = ctx->ctrl = sb_node_branch(ctx->func, ctx->ctrl, IN(0));

  SB_Node* branch_true = sb_node_branch_true(ctx->func, branch);
  SB_Node* branch_false = sb_node_branch_false(ctx->func, branch);

  push_block_entry(block_true, branch_true, ctx->mem);
  push_block_entry(block_false, branch_false, ctx->mem);

  return NULL;
}

SB_Func* lower_sem_func(SB_Context* sb, SemFunc* func) {
  Scratch scratch = scratch_get(0, NULL);

  SB_Func* sb_func = sb_begin_func(sb);

  SB_Node* start = sb_node_start(sb_func);
  SB_Node* start_ctrl = sb_node_start_ctrl(sb_func, start);
  SB_Node* start_mem = sb_node_start_mem(sb_func, start);

  int num_blocks = sem_assign_temp_ids(func);
  BlockData* block_data_map = arena_array(scratch.arena, BlockData, num_blocks);

  SB_Node** places = arena_array(scratch.arena, SB_Node*, vec_len(func->place_data));

  Vec(SB_Node*) v_end_ctrl = NULL;
  Vec(SB_Node*) v_end_mem = NULL;
  Vec(SB_Node*) v_end_val = NULL;

  for (int i = 0; i < vec_len(func->place_data); ++i) {
    places[i] = sb_node_alloca(sb_func);
  }

  for (SemBlock* block = func->cfg; block; block = block->next) {
    BlockData* block_data = &block_data_map[block->_id];

    block_data->region = sb_node_region(sb_func);
    block_data->mem_phi = sb_node_phi(sb_func);

    LowerCtx ctx = {
      .block_data_map = block_data_map,

      .func = sb_func,
      .ctrl = block_data->region,
      .mem = block_data->mem_phi,
      .places = places,

      .v_end_ctrl = &v_end_ctrl,
      .v_end_mem = &v_end_mem,
      .v_end_val = &v_end_val,
    };

    for (int i = 0; i < vec_len(block->code); ++i) {
      SemInst* inst = &block->code[i];

      SB_Node* result = NULL;

      #define X(name, ...) case SEM_OP_##name: result = lower_##name(&ctx, inst); break;
      switch (inst->op) {
        default:
          assert(false);
          break;
        #include "sem_op.def"
      }
      #undef X

      if (inst->write != SEM_NULL_PLACE) {
        assert(result != NULL);
        ctx.mem = sb_node_store(sb_func, ctx.ctrl, ctx.mem, places[inst->write], result);
      }
    }

    if (sem_compute_successors(block).count == 0 && !ctx.had_return) {
      push_func_exit(&ctx, ctx.ctrl, ctx.mem, sb_node_null(sb_func));
    }
  }

  push_block_entry(&block_data_map[0], start_ctrl, start_mem);

  for (int i = 0; i < num_blocks; ++i) {
    BlockData* data = &block_data_map[i];

    sb_set_region_ins(sb_func, data->region, vec_len(data->ctrl_in), data->ctrl_in);
    sb_set_phi_ins(sb_func, data->mem_phi, data->region, vec_len(data->mem_in), data->mem_in);

    vec_free(data->ctrl_in);
    vec_free(data->mem_in);
  }

  SB_Node* end_region = sb_node_region(sb_func);
  SB_Node* end_mem = sb_node_phi(sb_func);
  SB_Node* end_val = sb_node_phi(sb_func);

  sb_set_region_ins(sb_func, end_region, vec_len(v_end_ctrl), v_end_ctrl);
  sb_set_phi_ins(sb_func, end_mem, end_region, vec_len(v_end_mem), v_end_mem);
  sb_set_phi_ins(sb_func, end_val, end_region, vec_len(v_end_val), v_end_val);

  vec_free(v_end_ctrl);
  vec_free(v_end_mem);
  vec_free(v_end_val);

  sb_node_end(sb_func, end_region, sb_node_mem_escape(sb_func, end_mem), end_val);

  sb_finish_func(sb_func);

  scratch_release(&scratch);

  return sb_func;
}