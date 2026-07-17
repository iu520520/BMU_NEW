set(N32G455_SDK_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../third_party/N32G45x_SDK"
    CACHE PATH "Path to the official N32G45x firmware SDK"
)

if(NOT EXISTS "${N32G455_SDK_DIR}")
    message(FATAL_ERROR
        "N32G455 SDK not found.\n"
        "Expected path: ${N32G455_SDK_DIR}\n"
        "Place the official N32G45x SDK there, or configure with:\n"
        "  cmake -S code/BMU_NEW --preset n32g455-debug -DN32G455_SDK_DIR=<sdk-path>\n"
    )
endif()

message(STATUS "N32G455 SDK: ${N32G455_SDK_DIR}")
