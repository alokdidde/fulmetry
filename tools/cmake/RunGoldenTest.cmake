# Owner: S10. Runs one golden-image test; included by the script fulmetry_add_golden_test() generates.
cmake_minimum_required(VERSION 3.24)

function(golden_fail msg)
  # Keep everything needed to judge the failure in one folder CI uploads.
  file(MAKE_DIRECTORY "${GOLDEN_FAIL_DIR}")
  if(EXISTS "${GOLDEN_OUTPUT}")
    execute_process(COMMAND "${GOLDEN_IMGDIFF}" convert "${GOLDEN_OUTPUT}" "${GOLDEN_FAIL_DIR}/actual.png"
                    OUTPUT_QUIET)
  endif()
  if(EXISTS "${GOLDEN_FILE}")
    file(COPY_FILE "${GOLDEN_FILE}" "${GOLDEN_FAIL_DIR}/golden.png")
  endif()
  if(EXISTS "${GOLDEN_WORK_DIR}/diff.png")
    file(COPY_FILE "${GOLDEN_WORK_DIR}/diff.png" "${GOLDEN_FAIL_DIR}/diff.png")
  endif()
  message(FATAL_ERROR "GOLDEN-FAILED ${GOLDEN_NAME}: ${msg}\n  artifacts: ${GOLDEN_FAIL_DIR}")
endfunction()

file(REMOVE_RECURSE "${GOLDEN_FAIL_DIR}" "${GOLDEN_WORK_DIR}")
file(MAKE_DIRECTORY "${GOLDEN_WORK_DIR}")
file(REMOVE "${GOLDEN_OUTPUT}")

execute_process(COMMAND ${GOLDEN_COMMAND} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  string(REPLACE ";" " " shown "${GOLDEN_COMMAND}")
  golden_fail("command exited with ${rc}: ${shown}")
endif()
if(NOT EXISTS "${GOLDEN_OUTPUT}")
  golden_fail("command did not write ${GOLDEN_OUTPUT}")
endif()

if("$ENV{FULMETRY_UPDATE_GOLDENS}" AND NOT GOLDEN_EXPECT_MISMATCH)
  execute_process(COMMAND "${GOLDEN_IMGDIFF}" convert "${GOLDEN_OUTPUT}" "${GOLDEN_FILE}" RESULT_VARIABLE rc)
  if(NOT rc EQUAL 0)
    golden_fail("could not update ${GOLDEN_FILE}")
  endif()
  message(STATUS "updated golden ${GOLDEN_FILE}")
  return()
endif()

if(NOT EXISTS "${GOLDEN_FILE}")
  golden_fail("no golden ${GOLDEN_FILE}; create it with FULMETRY_UPDATE_GOLDENS=1")
endif()

execute_process(COMMAND "${GOLDEN_IMGDIFF}" compare "${GOLDEN_OUTPUT}" "${GOLDEN_FILE}"
                        --diff "${GOLDEN_WORK_DIR}/diff.png" ${GOLDEN_LIMITS}
                RESULT_VARIABLE rc)
if(GOLDEN_EXPECT_MISMATCH)
  # Only a real mismatch (exit 1) counts; a crash or I/O error (exit 2) must not pass as "expected failure".
  if(rc EQUAL 1)
    message(STATUS "${GOLDEN_NAME}: mismatch detected, as expected")
  elseif(rc EQUAL 0)
    golden_fail("expected a mismatch but the image is within tolerance: the limits are too loose")
  else()
    golden_fail("imgdiff error (exit ${rc})")
  endif()
elseif(NOT rc EQUAL 0)
  golden_fail("image differs from golden (imgdiff exit ${rc})")
endif()
