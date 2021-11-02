#!/bin/bash

export PMEM_IS_PMEM_FORCE=1

echo "Running map benchmark"
LD_LIBRARY_PATH=../nondebug ./pmembench pmembench_map.cfg

echo "Running tx benchmark"
LD_LIBRARY_PATH=../nondebug ./pmembench pmembench_tx.cfg

echo "Running tx_add_range benchmark"
LD_LIBRARY_PATH=../nondebug ./pmembench pmembench_tx_add_range.cfg
