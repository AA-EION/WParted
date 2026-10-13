cmake_minimum_required(VERSION 3.20)
project(WParted
    VERSION 1.0.0
    DESCRIPTION "WParted — Native Windows Partition Editor (Forked from GParted)"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable CTest
include(CTest)

# Options
option(ENABLE_GUI "Build GTKmm GUI application" ON)
option(BUILD_TESTS "Build test suite" ON)
option(BUILD_BOOT_HELPER "Build Native boot-time companion executable" ON)
option(BUILD_APFS_MOUNTER "Build APFS WinFsp mounter executable" ON)

# Configure config.h
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/config.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/include/config.h"
    @ONLY
)

# Common Include Directories
include_directories(
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)

# Windows definitions and compiler flags
if(WIN32)
    add_compile_definitions(
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
        UNICODE
        _UNICODE
        HAVE_CONFIG_H=1
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
    if(MSVC)
        add_compile_options(/utf-8 /W3 /EHsc)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
    else()
        add_compile_options(-Wall -Wextra -Wno-unused-parameter)
    endif()
endif()

# Core Windows Storage & Engine Library
set(WPARTED_CORE_SOURCES
    src/DualNomenclature.cc
    src/Win32StorageEngine.cc
    src/Win32VolumeLockGuard.cc
    src/BitLockerGuard.cc
    src/VdsWrapper.cc
    src/FmifsWrapper.cc
    src/WParted_Win32_Core.cc
    src/WParted_WSL_Bridge.cc
    src/ApfsDriver.cc
    src/WParted_Boot_Scheduler.cc
)

# Core headers
set(WPARTED_CORE_HEADERS
    include/DualNomenclature.h
    include/Win32StorageEngine.h
    include/Win32VolumeLockGuard.h
    include/BitLockerGuard.h
    include/VdsWrapper.h
    include/FmifsWrapper.h
    include/WParted_Win32_Core.h
    include/WParted_WSL_Bridge.h
    include/ApfsDriver.h
    include/ApfsStructures.h
    include/WParted_Boot_Job.h
    include/WParted_Boot_Scheduler.h
)

add_library(wparted_core STATIC
    ${WPARTED_CORE_SOURCES}
    ${WPARTED_CORE_HEADERS}
)

target_include_directories(wparted_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_BINARY_DIR}/include"
)

if(WIN32)
    target_link_libraries(wparted_core PUBLIC
        setupapi
        ole32
        oleaut32
        advapi32
        kernel32
        user32
        shell32
        mpr
        rstrtmgr
    )
endif()

# Resource file
set(WPARTED_RC "${CMAKE_CURRENT_SOURCE_DIR}/resources/wparted.rc")

# Check for GTKmm
find_package(PkgConfig QUIET)
set(GTKMM_FOUND FALSE)
if(PKG_CONFIG_FOUND)
    pkg_check_modules(GTKMM QUIET gtkmm-3.0)
    if(NOT GTKMM_FOUND)
        pkg_check_modules(GTKMM QUIET gtkmm-4.0)
    endif()
endif()

# WParted GUI Application
if(ENABLE_GUI)
    if(GTKMM_FOUND)
        message(STATUS "Found GTKmm: ${GTKMM_LIBRARIES}")
        
        # Original GParted GUI sources adapted for WParted
        set(WPARTED_GUI_SOURCES
            src/main.cc
            src/Win_WParted.cc
            src/WParted_Core.cc
            src/DrawingAreaVisualDisk.cc
            src/TreeView_Detail.cc
            src/HBoxOperations.cc
            src/ProgressBar.cc
            src/DialogManageFlags.cc
            src/Dialog_Partition_Resize_Move.cc
            src/Dialog_Partition_New.cc
            src/Dialog_Partition_Copy.cc
            src/Dialog_Partition_Info.cc
            src/Dialog_Partition_Name.cc
            src/Dialog_Disklabel.cc
            src/Dialog_FileSystem_Label.cc
            src/DialogFeatures.cc
            src/Dialog_Progress.cc
            src/DialogPasswordEntry.cc
            src/Dialog_Base_Partition.cc
            src/Frame_Resizer_Base.cc
            src/Frame_Resizer_Extended.cc
            src/OptionComboBox.cc
            src/MenuHelpers.cc
            src/Device.cc
            src/Partition.cc
            src/PartitionVector.cc
            src/PartitionLUKS.cc
            src/FileSystem.cc
            src/SupportedFileSystems.cc
            src/Operation.cc
            src/OperationCreate.cc
            src/OperationDelete.cc
            src/OperationFormat.cc
            src/OperationResizeMove.cc
            src/OperationCopy.cc
            src/OperationCheck.cc
            src/OperationLabelFileSystem.cc
            src/OperationNamePartition.cc
            src/OperationChangeUUID.cc
            src/OperationDetail.cc
            src/PasswordRAMStore.cc
            src/PipeCapture.cc
            src/CopyBlocks.cc
            src/BlockSpecial.cc
            src/Utils.cc
            src/FS_Info.cc
            src/Mount_Info.cc
            src/LUKS_Info.cc
            src/LVM2_Info.cc
            src/BCache_Info.cc
            src/SWRaid_Info.cc
            src/DMRaid.cc
            src/VGDevice.cc
            src/bcachefs.cc
            src/btrfs.cc
            src/exfat.cc
            src/ext2.cc
            src/f2fs.cc
            src/fat16.cc
            src/hfs.cc
            src/hfsplus.cc
            src/jfs.cc
            src/linux_swap.cc
            src/luks.cc
            src/lvm2_pv.cc
            src/minix.cc
            src/nilfs2.cc
            src/ntfs.cc
            src/reiser4.cc
            src/reiserfs.cc
            src/udf.cc
            src/xfs.cc
        )

        add_executable(wparted WIN32
            ${WPARTED_GUI_SOURCES}
            ${WPARTED_RC}
        )

        target_include_directories(wparted PRIVATE ${GTKMM_INCLUDE_DIRS})
        target_link_libraries(wparted PRIVATE wparted_core ${GTKMM_LIBRARIES})
        target_compile_options(wparted PRIVATE ${GTKMM_CFLAGS_OTHER})
    else()
        message(STATUS "GTKmm not found. Building standalone WParted executable.")
        add_executable(wparted
            src/wparted_main.cc
            ${WPARTED_RC}
        )
        target_link_libraries(wparted PRIVATE wparted_core)
    endif()
endif()

# APFS WinFsp Read-Only Mounter Executable
if(BUILD_APFS_MOUNTER)
    add_executable(wparted-apfs-mount
        src/wparted-apfs-mount.cc
        ${WPARTED_RC}
    )
    target_link_libraries(wparted-apfs-mount PRIVATE wparted_core)
endif()

# Windows Native Subsystem Early Boot Companion
if(BUILD_BOOT_HELPER AND WIN32)
    add_executable(wparted-boot
        src/wparted-boot.cc
    )
    if(MSVC)
        target_compile_options(wparted-boot PRIVATE /GS-)
        target_link_options(wparted-boot PRIVATE
            "/SUBSYSTEM:NATIVE"
            "/ENTRY:NtProcessStartup"
            "/NODEFAULTLIB"
        )
        set_target_properties(wparted-boot PROPERTIES
            LINK_FLAGS "/NODEFAULTLIB:kernel32.lib /NODEFAULTLIB:user32.lib /NODEFAULTLIB:gdi32.lib /NODEFAULTLIB:winspool.lib /NODEFAULTLIB:shell32.lib /NODEFAULTLIB:ole32.lib /NODEFAULTLIB:oleaut32.lib /NODEFAULTLIB:uuid.lib /NODEFAULTLIB:comdlg32.lib /NODEFAULTLIB:advapi32.lib"
        )
        target_link_libraries(wparted-boot PRIVATE ntdll.lib)
    else()
        target_link_options(wparted-boot PRIVATE
            "-Wl,--subsystem,native"
            "-Wl,-e,NtProcessStartup"
            "-nostdlib"
        )
        target_link_libraries(wparted-boot PRIVATE ntdll)
    endif()
endif()

# Tests
if(BUILD_TESTS)
    set(GTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/gtest")
    add_library(gtest STATIC
        "${GTEST_DIR}/src/gtest-all.cc"
        "${GTEST_DIR}/src/gtest_main.cc"
    )
    target_include_directories(gtest PUBLIC
        "${GTEST_DIR}/include"
        "${GTEST_DIR}"
    )

    add_executable(wparted_tests
        tests/test_win32_storage.cc
        tests/test_wsl_bridge.cc
        tests/test_apfs_parser.cc
        tests/test_boot_job.cc
        tests/test_dual_nomenclature.cc
    )
    target_link_libraries(wparted_tests PRIVATE
        gtest
        wparted_core
    )
    add_test(NAME AllWPartedTests COMMAND wparted_tests)
endif()
