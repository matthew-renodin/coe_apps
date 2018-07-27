#pragma once

#define INITIALIZATION_ERROR_NUMBER -1
#define INITIALIZATION_ERROR_STRING "Init objects (vka, vspace) have not been setup.\n" \
                                    "Run init_process or init_root_task to complete."

#define NULL_ARG_ERROR_NUMBER -2
#define NULL_ARG_ERROR_STRING "Null argument has been passed"

#define CAP_COPY_ERROR_NUMBER -3
#define CAP_COPY_ERROR_STRING "Failed to copy cap to process"

#define MALLOC_ERROR_NUMBER -4
#define MALLOC_ERROR_STRING "Failed to malloc data"

#define STATE_ERROR_NUMBER -5
#define STATE_ERROR_STRING "Process is not in proper state"