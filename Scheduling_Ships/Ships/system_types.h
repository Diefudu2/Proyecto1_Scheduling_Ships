#ifndef SYSTEM_TYPES_H
#define SYSTEM_TYPES_H

typedef enum {
    SCHED_FCFS = 0,
    SCHED_RR,
    SCHED_PRIORITY,
    SCHED_SJF,
    SCHED_STRN,
    SCHED_EDF
} SchedAlgo;

typedef enum {
    FLOW_TICO = 0,
    FLOW_EQUIDAD,
    FLOW_LETRERO
} FlowAlgo;

#endif