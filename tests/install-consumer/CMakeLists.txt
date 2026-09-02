cmake_minimum_required(VERSION 3.22)
project(rnsim_install_consumer LANGUAGES CXX)

find_package(ReactNativeSimulator CONFIG REQUIRED)

add_executable(rns-consumer main.cpp)
target_compile_features(rns-consumer PRIVATE cxx_std_20)
target_link_libraries(rns-consumer PRIVATE ReactNativeSimulator::Engine)
