# FreeRTOS kernel

This directory contains the minimal FreeRTOS kernel subset used by BMU_NEW.

- Source: Nations N32G45x Library 2.6.0
- FreeRTOS release: 202212.01
- Kernel version: 10.5.1
- Port: GCC Cortex-M4F
- Heap implementation: `heap_4.c`

Only `tasks.c`, `queue.c`, `list.c`, the Cortex-M4F port, the public headers,
and `heap_4.c` are included. The source files retain their upstream MIT
license notices.
