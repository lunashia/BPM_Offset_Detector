cmake_minimum_required(VERSION 3.16)
project(BPM_offset_detector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bpm_offset_detector_core
    # Stable pipeline sources (no A/C/test/debug variants).
    src/load_audio.cpp
    src/OnsetDetectorFFT.cpp
    src/tempo_estimator_b.cpp
    src/OffsetDetector.cpp
    src/FFT.cpp
    src/miniaudio_impl.cpp
)

target_include_directories(bpm_offset_detector_core
    PUBLIC
        # Public API headers consumed by external users and examples.
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        # Third-party single-header dependency implementation include path.
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

add_executable(bpm_offset_detector_example
    # Minimal release example: input audio path -> BPM + offset.
    examples/main.cpp
)

target_link_libraries(bpm_offset_detector_example
    PRIVATE bpm_offset_detector_core
)

enable_testing()

add_executable(bpm_offset_detector_test_unit
    tests/test_unit.cpp
)
target_link_libraries(bpm_offset_detector_test_unit
    PRIVATE bpm_offset_detector_core
)

add_executable(bpm_offset_detector_test_integration
    tests/test_integration.cpp
)
target_link_libraries(bpm_offset_detector_test_integration
    PRIVATE bpm_offset_detector_core
)

add_test(NAME bpm_offset_detector_unit COMMAND bpm_offset_detector_test_unit)
add_test(NAME bpm_offset_detector_integration COMMAND bpm_offset_detector_test_integration)
set_tests_properties(
    bpm_offset_detector_unit
    bpm_offset_detector_integration
    PROPERTIES WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
