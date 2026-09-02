# Source revisions validated together by react-native-simulator.
set(RNS_EXPECTED_REACT_NATIVE_COMMIT
    "4bc2473f5d0233ea5384c1ef24f6a55615de2220")
set(RNS_EXPECTED_HERMES_TAG "hermes-v260318099.0.1")
set(RNS_EXPECTED_FAST_FLOAT_TAG "v8.0.0")
set(RNS_EXPECTED_GLOG_COMMIT "7b134a5c82c0c0b5698bb6bf7a835b230c5638e4")
set(RNS_EXPECTED_IMGUI_TAG "v1.92.9")
set(RNS_EXPECTED_SDL_TAG "release-3.4.8")
set(RNS_EXPECTED_SKIA_COMMIT
    "cacf77bdba7ba7df8ea7236d7e14b08c658ff368")

# RN 0.87's RuntimeScheduler uses Hermes IEventLoopControl. The older
# sdks/.hermesv1version tag does not provide that API; this newer Hermes v1
# revision is the source combination validated by this host.
