#[[
add_library(sent_transition
  sent_transition.cpp
  mainnet.cpp
  testnet.cpp
  localdev.cpp
  devnet.cpp)
]]

add_library(sent_transition
  sent_transition.cpp
  localdev.cpp)

target_link_libraries(sent_transition
    PUBLIC
    cryptonote_core)
