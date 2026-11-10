# Applies a git patch once (idempotent): skipped when it is already applied.
# Usage: cmake -DPATCH=<file> -P ApplyPatch.cmake   (working directory = source tree)
execute_process(COMMAND git apply --reverse --check --ignore-whitespace "${PATCH}"
    RESULT_VARIABLE alreadyApplied OUTPUT_QUIET ERROR_QUIET)
if(alreadyApplied EQUAL 0)
    return()
endif()
execute_process(COMMAND git apply --ignore-whitespace "${PATCH}" RESULT_VARIABLE result)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Failed to apply ${PATCH}")
endif()
