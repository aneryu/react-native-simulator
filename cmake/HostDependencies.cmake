# Host library lookup for macOS (Homebrew) and Linux (system + RN Folly subset).
#
# Linux has no distro Folly package that matches RN 0.87. Compile the same
# folly_runtime sources Android uses, fetched from the pinned Folly tag.
# This file is included from runtime/CMakeLists.txt at directory scope.

include(FetchContent)

find_package(Boost CONFIG QUIET COMPONENTS headers)
if(NOT Boost_FOUND)
  find_package(Boost REQUIRED)
endif()
if(NOT TARGET Boost::headers AND TARGET Boost::boost)
  add_library(Boost::headers ALIAS Boost::boost)
endif()
if(NOT TARGET Boost::boost AND TARGET Boost::headers)
  add_library(Boost::boost ALIAS Boost::headers)
endif()
find_package(Threads REQUIRED)
find_package(fmt CONFIG REQUIRED)

if(APPLE)
  find_path(FOLLY_INCLUDE_DIR folly/dynamic.h
    PATHS /opt/homebrew/include /usr/local/include)
  if(NOT FOLLY_INCLUDE_DIR)
    message(FATAL_ERROR
      "folly headers are required (for example: brew install folly).")
  endif()
  find_file(FOLLY_STATIC_LIBRARY NAMES libfolly.a REQUIRED
    PATHS /opt/homebrew/lib /usr/local/lib)
  find_file(RNS_DOUBLE_CONVERSION_STATIC_LIBRARY NAMES libdouble-conversion.a REQUIRED
    PATHS /opt/homebrew/lib /usr/local/lib)
  find_file(RNS_FMT_STATIC_LIBRARY NAMES libfmt.a REQUIRED
    PATHS /opt/homebrew/lib /usr/local/lib)
  if(RNS_ENABLE_SANITIZERS)
    find_library(RNS_FOLLY_SHARED_LIBRARY NAMES folly REQUIRED
      PATHS /opt/homebrew/lib /usr/local/lib)
    find_library(RNS_DOUBLE_CONVERSION_SHARED_LIBRARY NAMES double-conversion REQUIRED
      PATHS /opt/homebrew/lib /usr/local/lib)
  endif()
  add_library(rns_folly_headers INTERFACE)
  target_include_directories(rns_folly_headers INTERFACE "${FOLLY_INCLUDE_DIR}")
  add_library(rns_folly INTERFACE)
  target_link_libraries(rns_folly INTERFACE rns_folly_headers)
  if(RNS_ENABLE_SANITIZERS)
    target_link_libraries(rns_folly INTERFACE
      "${RNS_FOLLY_SHARED_LIBRARY}"
      glog::glog
      "${RNS_DOUBLE_CONVERSION_SHARED_LIBRARY}")
  else()
    target_link_libraries(rns_folly INTERFACE
      "-Wl,-load_hidden,${FOLLY_STATIC_LIBRARY}"
      glog::glog
      "${RNS_FMT_STATIC_LIBRARY}"
      "${RNS_DOUBLE_CONVERSION_STATIC_LIBRARY}")
  endif()
else()
  find_library(RNS_DOUBLE_CONVERSION_LIBRARY NAMES double-conversion REQUIRED)
  find_package(CURL REQUIRED)
  find_package(OpenSSL REQUIRED)
  find_package(PNG REQUIRED)
  find_package(ZLIB REQUIRED)
  find_library(UUID_LIBRARY NAMES uuid REQUIRED)

  FetchContent_Declare(rns_folly_src
    URL "https://github.com/facebook/folly/archive/refs/tags/v${RNS_EXPECTED_FOLLY_VERSION}.tar.gz"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_GetProperties(rns_folly_src)
  if(NOT rns_folly_src_POPULATED)
    message(STATUS
      "Fetching Folly ${RNS_EXPECTED_FOLLY_VERSION} (RN folly_runtime subset)")
    FetchContent_Populate(rns_folly_src)
  endif()
  set(FOLLY_INCLUDE_DIR "${rns_folly_src_SOURCE_DIR}")
  if(NOT EXISTS "${FOLLY_INCLUDE_DIR}/folly/json/json.cpp")
    message(FATAL_ERROR
      "Folly ${RNS_EXPECTED_FOLLY_VERSION} is missing json.cpp under ${FOLLY_INCLUDE_DIR}")
  endif()

  set(folly_runtime_sources
    "${FOLLY_INCLUDE_DIR}/folly/Conv.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/FileUtil.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/Format.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/ScopeGuard.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/SharedMutex.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/String.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/Unicode.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/concurrency/CacheLocality.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/container/detail/F14Table.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/detail/FileUtilDetail.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/detail/Futex.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/detail/SplitStringSimd.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/detail/StaticSingletonManager.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/detail/UniqueInstance.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/hash/SpookyHashV2.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/json/dynamic.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/json/json_pointer.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/json/json.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/lang/CString.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/lang/Exception.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/lang/SafeAssert.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/lang/ToAscii.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/memory/detail/MallocImpl.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/memory/SanitizeLeak.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/net/NetOps.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/portability/SysUio.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/synchronization/SanitizeThread.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/synchronization/ParkingLot.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/system/AtFork.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/system/ThreadId.cpp"
    "${FOLLY_INCLUDE_DIR}/folly/system/ThreadName.cpp")
  set(folly_runtime_existing)
  foreach(source IN LISTS folly_runtime_sources)
    if(EXISTS "${source}")
      list(APPEND folly_runtime_existing "${source}")
    endif()
  endforeach()

  add_library(rns_folly_headers INTERFACE)
  target_include_directories(rns_folly_headers INTERFACE "${FOLLY_INCLUDE_DIR}")
  target_compile_definitions(rns_folly_headers INTERFACE
    FOLLY_NO_CONFIG=1
    FOLLY_HAVE_CLOCK_GETTIME=1
    FOLLY_CFG_NO_COROUTINES=1
    FOLLY_HAVE_PTHREAD=1
    FOLLY_HAVE_RECVMMSG=1
    FOLLY_MOBILE=1
    FOLLY_HAVE_LIBGFLAGS=0
    FOLLY_HAVE_LIBJEMALLOC=0)

  add_library(rns_folly_runtime STATIC ${folly_runtime_existing})
  set_target_properties(rns_folly_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON)
  target_compile_options(rns_folly_runtime PRIVATE
    -fexceptions -frtti -Wno-sign-compare -Wno-unused-parameter)
  target_link_libraries(rns_folly_runtime PUBLIC
    rns_folly_headers
    glog::glog
    fmt::fmt
    Boost::headers
    FastFloat::fast_float
    "${RNS_DOUBLE_CONVERSION_LIBRARY}"
    Threads::Threads)
  if(RNS_ENABLE_SANITIZERS)
    target_compile_options(rns_folly_runtime PRIVATE
      -fsanitize=address,undefined -fno-sanitize=vptr
      -fno-omit-frame-pointer)
    target_link_options(rns_folly_runtime PUBLIC -fsanitize=address,undefined)
  endif()

  add_library(rns_folly INTERFACE)
  target_link_libraries(rns_folly INTERFACE rns_folly_runtime)
endif()
