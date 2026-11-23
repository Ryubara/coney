# SPDX-License-Identifier: GPL-3.0-or-later
# Plays level99 headless from a script with `--record-input`, then plays the recording back with `--input-script`
# alone, and passes when both runs trace player 1 and the camera identically on every step: the recording is the run
# (docs/guides/run-log.md#input-recording). The second run takes its AI seed from the recording's header, which its
# output says. Counts only; the traces stay in the work folder, which the test makes afresh. Variables: CONEY_EXE,
# DISC, SCRIPT (the first run's input script), WORK (the folder), HANG_GUARD (seconds).
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(common --disc "${DISC}" --headless --frames 510 --play-level level99 --skip-movies)

execute_process(COMMAND "${CONEY_EXE}" ${common} --ai-seed 3 --input-script "${SCRIPT}"
                        --record-input "${WORK}/recording.txt" --trace "${WORK}/first.csv"
                TIMEOUT ${HANG_GUARD} RESULT_VARIABLE first_result OUTPUT_VARIABLE first_out ERROR_VARIABLE first_err)
if(NOT first_result EQUAL 0)
    message(FATAL_ERROR "the recorded run failed (${first_result}):\n${first_out}${first_err}")
endif()
if(NOT EXISTS "${WORK}/recording.txt")
    message(FATAL_ERROR "the recorded run wrote no recording:\n${first_out}${first_err}")
endif()
file(READ "${WORK}/recording.txt" recording)
if(NOT recording MATCHES "# ai-seed 3\n" OR NOT recording MATCHES "# play-level level99\n"
   OR NOT recording MATCHES "# frames [1-9][0-9]*\n")
    message(FATAL_ERROR "the recording's header or footer is missing")
endif()

execute_process(COMMAND "${CONEY_EXE}" ${common} --input-script "${WORK}/recording.txt" --trace "${WORK}/second.csv"
                TIMEOUT ${HANG_GUARD} RESULT_VARIABLE second_result OUTPUT_VARIABLE second_out
                ERROR_VARIABLE second_err)
if(NOT second_result EQUAL 0)
    message(FATAL_ERROR "the replay failed (${second_result}):\n${second_out}${second_err}")
endif()
if(NOT "${second_out}${second_err}" MATCHES "ai-seed 3 from the recording's header")
    message(FATAL_ERROR "the replay did not take the recording's seed:\n${second_out}${second_err}")
endif()

file(SHA256 "${WORK}/first.csv" first_hash)
file(SHA256 "${WORK}/second.csv" second_hash)
file(SIZE "${WORK}/first.csv" first_size)
if(first_size EQUAL 0)
    message(FATAL_ERROR "the trace is empty")
endif()
if(NOT first_hash STREQUAL second_hash)
    message(FATAL_ERROR "the replay's trace differs from the recorded run's (${first_hash} against ${second_hash})")
endif()
message(STATUS "replayed ${first_size} bytes of trace identically")
