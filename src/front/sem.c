#include <stdio.h>

#include "front.h"

void free_sem_func_storage(SemFunc* func) {
  foreach_list(SemBlock, b, func->cfg) {
    vec_free(b->code);
  }

  vec_free(func->place_data);
}

static int assign_temp_ids(SemFunc* func) {
  int next_block_id = 0;

  foreach_list(SemBlock, b, func->cfg) {
    b->_id = next_block_id++;
  }

  return next_block_id;
}

void print_sem_func(SemFunc* func) {
  assign_temp_ids(func);

  foreach_list(SemBlock, b, func->cfg) {
    printf("bb_%d:\n", b->_id);

    for(int inst_idx = 0; inst_idx < vec_len(b->code); ++inst_idx) {
      printf("  ");

      SemInst* inst = &b->code[inst_idx];

      if (inst->write != SEM_NULL_PLACE) {
        printf("_%-3d = ", inst->write);
      }
      else {
        printf("%7s", "");
      }

      printf("%s ", sem_op_label[inst->op]);

      for (int i = 0; i < inst->num_reads; ++i) {
        if (i > 0) {
          printf(", ");
        }

        printf("_%d", inst->reads[i]);
      }

      switch (inst->op) {
        case SEM_OP_INTEGER_CONST:
          printf("%lld", (uint64_t)inst->data);
          break;

        case SEM_OP_GOTO: {
          SemBlock* loc = inst->data;
          printf("bb_%d", loc->_id);
        } break;

        case SEM_OP_BRANCH: {
          SemBlock** locs = inst->data;
          printf(" [bb_%d, bb_%d]", locs[0]->_id, locs[1]->_id);
        } break;
      }

      printf("\n");
    }
  }

  printf("\n");
}

typedef struct {
  int count;
  SemBlock* data[2];
} Successors;

static Successors compute_successors(SemBlock* block) {
  Successors result = {0};

  if (vec_len(block->code)) {
    SemInst* inst = vec_back(block->code);

    switch (inst->op) {
      case SEM_OP_GOTO:
        result.data[result.count++] = inst->data;
        break;
      
      case SEM_OP_BRANCH: {
        SemBlock** locs = inst->data;
        result.data[result.count++] = locs[0];
        result.data[result.count++] = locs[1];
      } break;
    }
  }

  return result;
}

bool sem_analyze_func(const char* path, const char* source, SemFunc* func) {
  Scratch scratch = scratch_get(0, NULL);

  bool success = true;

  int num_blocks = assign_temp_ids(func);

  uint64_t* reachable = arena_array(scratch.arena, uint64_t, bitset_num_u64(num_blocks));

  Vec(SemBlock*) stack = NULL;
  vec_put(stack, func->cfg);

  while (vec_len(stack)) {
    SemBlock* block = vec_pop(stack);

    if (bitset_get(reachable, block->_id)) {
      continue;
    }

    bitset_set(reachable, block->_id);

    Successors succ = compute_successors(block);

    for (int i = 0; i < succ.count; ++i) {
      vec_put(stack, succ.data[i]);
    }
  }

  for (SemBlock** pb = &func->cfg; *pb;) {
    SemBlock* b = *pb;

    if (bitset_get(reachable, b->_id)) {
      pb = &b->next;
      continue;
    }

    if (b->contains_usercode) {
      error_token(path, source, b->code[0].token, "this code is unreachable");
      success = false;
    }

    *pb = b->next;
  }

  vec_free(stack);
  scratch_release(&scratch);

  return success;
}