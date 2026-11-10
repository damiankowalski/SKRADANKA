# Runs "PenumbraEditor --mcp" as an MCP client would: JSON-RPC messages on stdin, one response per
# line on stdout. Fails if stdout carries anything but protocol messages or a response is wrong.
#   cmake -DEDITOR=<PenumbraEditor.exe> -DSESSION=<StdioSession.jsonl> -P StdioTest.cmake

execute_process(
    COMMAND "${EDITOR}" --mcp --backend headless --warp
    INPUT_FILE "${SESSION}"
    OUTPUT_VARIABLE output
    ERROR_VARIABLE log
    RESULT_VARIABLE exitCode
    TIMEOUT 300)
if(NOT exitCode EQUAL 0)
    message(FATAL_ERROR "PenumbraEditor --mcp exited with ${exitCode}\n${log}")
endif()

set(rest "${output}")
set(count 0)
while(TRUE)
    string(FIND "${rest}" "\n" newline)
    if(newline EQUAL -1)
        break()
    endif()
    string(SUBSTRING "${rest}" 0 ${newline} line)
    math(EXPR next "${newline} + 1")
    string(SUBSTRING "${rest}" ${next} -1 rest)
    string(JSON id ERROR_VARIABLE error GET "${line}" id)
    if(error)
        message(FATAL_ERROR "stdout line is not a JSON-RPC response: ${line}")
    endif()
    set(response_${id} "${line}")
    math(EXPR count "${count} + 1")
endwhile()
string(STRIP "${rest}" rest)
if(NOT rest STREQUAL "")
    message(FATAL_ERROR "unterminated stdout data: ${rest}")
endif()
if(NOT count EQUAL 7)
    message(FATAL_ERROR "expected 7 responses (the notification gets none), got ${count}:\n${output}")
endif()

function(expect id path expected)
    string(JSON value ERROR_VARIABLE error GET "${response_${id}}" ${path})
    if(error OR NOT "${value}" STREQUAL "${expected}")
        message(FATAL_ERROR "response ${id}: ${path} is '${value}', expected '${expected}' ${error}\n${response_${id}}")
    endif()
endfunction()

expect(1 "result;protocolVersion" "2025-11-25")
expect(1 "result;serverInfo;name" "penumbra")
string(FIND "${response_2}" "\"name\":\"entity_create\"" found)
if(found EQUAL -1)
    message(FATAL_ERROR "tools/list has no entity_create tool:\n${response_2}")
endif()
expect(3 "result;isError" "OFF")
expect(4 "result;isError" "OFF")
expect(4 "result;content;0;type" "image")
expect(4 "result;content;0;mimeType" "image/png")
expect(5 "result;isError" "ON")
expect(6 "error;code" "-32601")
expect(7 "result;isError" "OFF")
message(STATUS "MCP stdio session OK (${count} responses)")
