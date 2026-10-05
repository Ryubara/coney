# Runs error_dialogs_check (CHECK_EXE) and passes when the debug runtime's report reached the output and the process
# ended by itself. It ends by aborting, which ctest alone would count as a failure whatever the output; a hang on a
# dialog shows here as the timeout.
# Two variables: with one for both streams, CMake loses the runtime's report (seen with CMake 3.31 on Windows).
execute_process(COMMAND "${CHECK_EXE}" TIMEOUT 30 RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
set(out "${stdout}${stderr}")
if(result MATCHES "timeout")
    message(FATAL_ERROR "error_dialogs_check did not end: an error dialog is waiting for a click")
endif()
if(NOT out MATCHES "called on empty vector")
    message(FATAL_ERROR "the debug runtime's report did not reach the output (result ${result}):\n${out}")
endif()
