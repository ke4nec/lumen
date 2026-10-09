if(NOT DEFINED DESIGNER OR NOT DEFINED GALLERY)
  message(FATAL_ERROR "Designer executable and gallery are required")
endif()

set(fixture_root "${CMAKE_CURRENT_BINARY_DIR}/designer-diagnostic-fixtures")
file(REMOVE_RECURSE "${fixture_root}")
file(MAKE_DIRECTORY "${fixture_root}")

function(check_diagnostics filename expected_code expected_stage expected_line)
  execute_process(
      COMMAND "${DESIGNER}" --headless --dump-diagnostics --file "${filename}"
      RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  string(REPLACE "\r\n" "\n" output "${output}")
  if(NOT result EQUAL 1)
    message(FATAL_ERROR "invalid document returned ${result}: ${output} ${error}")
  endif()
  string(REGEX MATCH "diagnostics_json ([^\n]*)" record "${output}")
  if(NOT record)
    message(FATAL_ERROR "structured diagnostic missing: ${output}")
  endif()
  set(json "${CMAKE_MATCH_1}")
  string(JSON count LENGTH "${json}")
  if(count LESS 1 OR NOT output MATCHES "diagnostics ${count}\n")
    message(FATAL_ERROR "aggregated diagnostic count differs: ${output}")
  endif()
  foreach(field code severity stage message expected found file sourceSpan
                documentId nodeId nodePath property related recoverability occurrences)
    string(JSON value GET "${json}" 0 "${field}")
  endforeach()
  string(JSON code GET "${json}" 0 code)
  string(JSON stage GET "${json}" 0 stage)
  string(JSON file GET "${json}" 0 file)
  string(JSON recovery GET "${json}" 0 recoverability)
  string(JSON line GET "${json}" 0 sourceSpan begin line)
  if(NOT code STREQUAL expected_code OR NOT stage STREQUAL expected_stage)
    message(FATAL_ERROR "wrong diagnostic origin: ${json}")
  endif()
  if(NOT file STREQUAL filename OR NOT recovery STREQUAL "keep-last-frame"
      OR NOT line EQUAL expected_line)
    message(FATAL_ERROR "lost file/location/recovery: ${json}")
  endif()
  # A repeated input must emit exactly the same normalized diagnostics.
  execute_process(
      COMMAND "${DESIGNER}" --headless --dump-diagnostics --file "${filename}"
      RESULT_VARIABLE repeated OUTPUT_VARIABLE repeated_output)
  string(REGEX MATCH "diagnostics_json ([^\n]*)" repeated_record "${repeated_output}")
  if(NOT repeated EQUAL result OR NOT CMAKE_MATCH_1 STREQUAL json)
    message(FATAL_ERROR "diagnostic output is nondeterministic")
  endif()
endfunction()

check_diagnostics("${fixture_root}/missing.lumen" "read.io" "read" 1)
check_diagnostics("${fixture_root}/missing.design" "store.read" "read" 1)
check_diagnostics("${fixture_root}/missing.lumen-project" "project.read" "read" 1)
file(WRITE "${fixture_root}/broken.lumen" "page broken {\n")
check_diagnostics("${fixture_root}/broken.lumen" "parse.error" "parse" 2)
file(WRITE "${fixture_root}/broken.lumen-project" "{")
check_diagnostics("${fixture_root}/broken.lumen-project" "project.codec.expected_token" "read" 1)
file(WRITE "${fixture_root}/future.lumen-project"
    "{\"format\":\"lumen.project\",\"schemaVersion\":2,\"projectId\":\"future\",\"name\":\"Future\",\"root\":\".\",\"pages\":[],\"resources\":[],\"references\":[]}")
check_diagnostics("${fixture_root}/future.lumen-project" "project.schema_version" "schema" 1)

execute_process(
    COMMAND "${DESIGNER}" --headless --dump-diagnostics --file "${GALLERY}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0 OR NOT output MATCHES "diagnostics_json \\[\\]")
  message(FATAL_ERROR "valid gallery did not emit an empty diagnostic array: ${output} ${error}")
endif()
file(REMOVE_RECURSE "${fixture_root}")
