cmake_minimum_required(VERSION 3.20)
project(snuggle-bench CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Root paths (configure these to point at your OrcaSlicer checkout) ──
# Clone OrcaSlicer and build it first, then set ORCA_ROOT to its location.
# Example: cmake -B build -DORCA_ROOT=C:/dev/OrcaSlicer
if(NOT DEFINED ORCA_ROOT)
    message(FATAL_ERROR
        "ORCA_ROOT not set. Point it at your OrcaSlicer source tree.\n"
        "  cmake -B build -DORCA_ROOT=/path/to/OrcaSlicer\n"
        "OrcaSlicer must be built first (Release config).")
endif()

set(ORCA_SRC    "${ORCA_ROOT}/src")
set(ORCA_BUILD  "${ORCA_ROOT}/build")
set(ORCA_DEPS   "${ORCA_ROOT}/deps/build/OrcaSlicer_dep/usr/local")
set(ORCA_DEPSRC "${ORCA_ROOT}/deps_src")
set(OPENCV_BUILD "${ORCA_ROOT}/deps/build/dep_OpenCV-prefix/src/dep_OpenCV-build")
set(OPENCV_SRC   "${ORCA_ROOT}/deps/build/dep_OpenCV-prefix/src/dep_OpenCV")

# ── Include paths ───────────────────────────────────────────
include_directories(
    ${ORCA_SRC}
    ${ORCA_SRC}/libslic3r
    ${ORCA_DEPSRC}/eigen
    ${ORCA_DEPSRC}/admesh
    ${ORCA_DEPSRC}/clipper
    ${ORCA_DEPSRC}/clipper2/CPP/Clipper2Lib/include
    ${ORCA_DEPSRC}/libnest2d/include
    ${ORCA_DEPSRC}/qhull/src
    ${ORCA_DEPSRC}/mcut/include
    ${ORCA_DEPSRC}/Shiny/include
    ${ORCA_DEPSRC}/miniz
    ${ORCA_DEPSRC}/qoi
    ${ORCA_DEPSRC}/expat/lib
    ${ORCA_DEPSRC}/glu-libtess/include
    ${ORCA_DEPSRC}/semver
    ${ORCA_DEPSRC}
    ${ORCA_DEPS}/include
    ${ORCA_DEPS}/include/boost-1_84
    ${ORCA_DEPS}/include/occt
    ${OPENCV_SRC}/include
    ${OPENCV_BUILD}/modules/core/include
    ${OPENCV_BUILD}
    # Generated config headers
    ${ORCA_BUILD}
    ${ORCA_BUILD}/src
    ${ORCA_BUILD}/src/libslic3r
)

# ── Preprocessor defines (match Orca's build) ──────────────
add_definitions(
    -DWIN32
    -D_WIN32
    -DNDEBUG
    -DNOMINMAX
    -D_USE_MATH_DEFINES
    -DHAS_GLSAFE
    -DBOOST_ALL_NO_LIB
    -D_SILENCE_ALL_CXX17_DEPRECATION_WARNINGS
    -DSLIC3R_LOGLEVEL=3
)

# ── Library paths ───────────────────────────────────────────
link_directories(
    ${ORCA_BUILD}/src/libslic3r/Release
    ${ORCA_BUILD}/deps_src/admesh/Release
    ${ORCA_BUILD}/deps_src/clipper/Release
    ${ORCA_BUILD}/deps_src/clipper2/Release
    ${ORCA_BUILD}/deps_src/libnest2d/Release
    ${ORCA_BUILD}/deps_src/qhull/Release
    ${ORCA_BUILD}/deps_src/mcut/Release
    ${ORCA_BUILD}/deps_src/glu-libtess/Release
    ${ORCA_BUILD}/deps_src/miniz/Release
    ${ORCA_BUILD}/deps_src/qoi/Release
    ${ORCA_BUILD}/deps_src/Shiny/Release
    ${ORCA_BUILD}/deps_src/expat/Release
    ${ORCA_BUILD}/lib/Release
    ${ORCA_DEPS}/lib
    ${ORCA_DEPS}/lib/occt
    ${OPENCV_BUILD}/lib/Release
)

# ── The test harness ────────────────────────────────────────
add_executable(arrange_baseline baseline_arrange.cpp nsvg_stub.cpp)
add_executable(test_voxelizer test_voxelizer.cpp nsvg_stub.cpp)
add_executable(test_snuggle test_snuggle.cpp nsvg_stub.cpp)
add_executable(test_sat test_sat.cpp nsvg_stub.cpp)
add_executable(sweep_resolution sweep_resolution.cpp nsvg_stub.cpp)
add_executable(sweep_full sweep_full.cpp nsvg_stub.cpp)

set(ORCA_LINK_LIBS
    # Core
    libslic3r
    libslic3r_cgal
    admesh
    libnest2d
    clipper
    Clipper2
    qhullstatic
    mcut
    glu-libtess
    miniz_static
    qoi
    Shiny
    expat
    semver

    # Deps
    libnoise_static
    nlopt
    tbb12
    tbbmalloc
    opencv_world460
    draco
    libopenvdb
    libblosc

    # Boost
    libboost_filesystem-vc144-mt-x64-1_84
    libboost_log-vc144-mt-x64-1_84
    libboost_log_setup-vc144-mt-x64-1_84
    libboost_thread-vc144-mt-x64-1_84
    libboost_locale-vc144-mt-x64-1_84
    libboost_iostreams-vc144-mt-x64-1_84
    libboost_chrono-vc144-mt-x64-1_84
    libboost_date_time-vc144-mt-x64-1_84
    libboost_atomic-vc144-mt-x64-1_84
    libboost_nowide-vc144-mt-x64-1_84
    libboost_random-vc144-mt-x64-1_84
    libboost_serialization-vc144-mt-x64-1_84

    # Image / compression
    libpng16_static
    jpeg-static
    turbojpeg-static
    zlib
    Half-2_5
    Iex-2_5
    IexMath-2_5
    IlmImf-2_5
    IlmThread-2_5
    Imath-2_5

    # Crypto / network (libslic3r links OpenSSL)
    libcrypto
    libssl
    libcurl

    # GMP/MPFR (for CGAL)
    libgmp-10
    libmpfr-4

    # OpenCASCADE (STEP import, linked by libslic3r)
    TKXDESTEP TKSTEPBase TKSTEPAttr TKSTEP209 TKSTEP
    TKXCAF TKCAF TKCDF TKLCAF
    TKXSBase TKShHealing TKTopAlgo TKGeomAlgo
    TKBRep TKGeomBase TKG3d TKG2d TKMath TKernel
    TKMesh TKPrim TKBO TKBool TKHLR
    TKSTL TKService TKV3d
    TKBin TKBinL TKBinXCAF
    TKXml TKXmlL TKXmlXCAF
    TKVCAF TKVRML TKRWMesh
    TKXDEIGES TKIGES

    # Windows system
    Psapi bcrypt ws2_32 crypt32 wldap32 Advapi32 User32
)

target_link_libraries(arrange_baseline ${ORCA_LINK_LIBS})
target_link_libraries(test_voxelizer ${ORCA_LINK_LIBS})
target_link_libraries(test_snuggle ${ORCA_LINK_LIBS})
target_link_libraries(test_sat ${ORCA_LINK_LIBS})
target_link_libraries(sweep_resolution ${ORCA_LINK_LIBS})
target_link_libraries(sweep_full ${ORCA_LINK_LIBS})
