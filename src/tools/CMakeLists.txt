add_executable(generate_genesis generate_genesis.cpp)
target_link_libraries(generate_genesis
    PRIVATE
        cryptonote_core
        cryptonote_basic
        common
        cncrypto
        extra) 

add_executable(find_prefix find_prefix.cpp)
target_link_libraries(find_prefix
    PRIVATE
        cryptonote_core
        cryptonote_basic
        common) 