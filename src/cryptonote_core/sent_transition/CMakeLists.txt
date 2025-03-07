add_library(sent_transition_data
  devnet.cpp
  localdev.cpp
  testnet.cpp
  transition_data.cpp)

target_link_libraries(sent_transition_data
    PUBLIC
    common
    oxenc::oxenc)
