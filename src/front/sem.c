#include <stdio.h>

#include "front.h"

void free_sem_func_storage(SemFunc* func) {
  foreach_list(SemBlock, b, func->cfg) {
    vec_free(b->code);
  }

  vec_free(func->place_data);
}

void print_sem_func(SemFunc* func) {
  int next_block_id = 0;

  foreach_list(SemBlock, b, func->cfg) {
    b->_id = next_block_id++;
  }

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