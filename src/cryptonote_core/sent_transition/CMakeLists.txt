oxen_add_library(sent_transition_data
  devnet.cpp
  localdev.cpp
  mainnet.cpp
  testnet.cpp)

if (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(sent_transition_data PUBLIC -fno-var-tracking)
endif()

target_link_libraries(sent_transition_data
    PUBLIC
    common
    oxenc::oxenc)
