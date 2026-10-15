# WParted - Windows Partition Editor, a Windows port of GParted
#
# Builds:
#   wparted             The GTKmm 3 GUI (Windows only; MSYS2 UCRT64 / MinGW-w64)
#   wparted_unit_tests  Platform independent unit tests (Windows and Linux)
#   wparted_win_tests   Windows integration tests against a virtual disk (VHDX)
#
# Typical Windows build in an MSYS2 UCRT64 shell:
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   ctest --test-dir build
#
# Unit tests only, on Linux:
#   cmake -S . -B build -DWPARTED_BUILD_GUI=OFF && cmake --build build && ctest --test-dir build

cmake_minimum_required(VERSION 3.20)

project(WParted
	VERSION 0.1.0
	DESCRIPTION "Windows Partition Editor - a Windows port of GParted"
	HOMEPAGE_URL "https://github.com/AA-EION/WParted"
	LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WPARTED_BUILD_GUI   "Build the wparted GUI (Windows, requires gtkmm-3.0)" ON)
option(WPARTED_BUILD_TESTS "Build the tests" ON)
option(WPARTED_WERROR      "Treat compiler warnings in WParted's own code as errors" OFF)

if(WPARTED_BUILD_GUI AND NOT WIN32)
	message(FATAL_ERROR
		"The WParted GUI is Windows only (on Linux use GParted itself).  "
		"Configure with -DWPARTED_BUILD_GUI=OFF to build and run the unit tests.")
endif()

if(MSVC)
	message(FATAL_ERROR
		"WParted is built with MinGW-w64 GCC (MSYS2 UCRT64): gtkmm 3 is not available for MSVC.")
endif()

include(GNUInstallDirs)
find_package(PkgConfig)


# ---------------------------------------------------------------------------
# Windows partition table layer.  Platform independent except for the Win32
# backend and volume code; the same sources are unit tested on Linux.

add_library(wparted_disk STATIC
	src/WinDiskModel.cc
	src/WinDevNames.cc
	src/WinBroker.cc
	src/WinSandbox.cc
	src/WinCapabilities.cc
	src/WinFsUsage.cc
	src/WinSwap.cc
	src/WinHelpers.cc
	src/WinComponents.cc
	src/WinFsProbe.cc
	src/WinScan.cc
	src/PartedWin32Bridge.cc
	src/Win32DiskBackend.cc
	$<$<BOOL:${WIN32}>:src/Win32Volumes.cc>
	$<$<BOOL:${WIN32}>:src/WinVolumeOps.cc>
)
target_include_directories(wparted_disk PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(wparted_disk PUBLIC WPARTED_VERSION_STRING="${PROJECT_VERSION}")
target_compile_options(wparted_disk PRIVATE -Wall -Wextra -Wno-unused-parameter
	$<$<BOOL:${WPARTED_WERROR}>:-Werror>)
if(WIN32)
	target_compile_definitions(wparted_disk PUBLIC
		_WIN32_WINNT=0x0A00 WINVER=0x0A00 UNICODE _UNICODE NOMINMAX)
	target_link_libraries(wparted_disk PUBLIC setupapi ole32 oleaut32 uuid wbemuuid advapi32)
endif()


# Client side of the disk broker, linked into the helper programs.
add_library(wparted_broker_client STATIC helpers/broker/wpbroker.c)
target_include_directories(wparted_broker_client PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/helpers/broker")
target_compile_options(wparted_broker_client PRIVATE -Wall -Wextra $<$<BOOL:${WPARTED_WERROR}>:-Werror>)


# ---------------------------------------------------------------------------
# GUI

if(WPARTED_BUILD_GUI)
	pkg_check_modules(GTKMM REQUIRED IMPORTED_TARGET gtkmm-3.0>=3.22)
	set(HAVE_SET_PROPAGATE_NATURAL_WIDTH 1)
	set(HAVE_GTK_SHOW_URI_ON_WINDOW 1)
	set(ENABLE_NLS 1)
	set(WPARTED_LOCALEDIR "share/locale")
	configure_file(include/config.h.in "${CMAKE_CURRENT_BINARY_DIR}/include/config.h")

	# The GParted sources, plus the Windows file system implementation.
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
		src/Proc_Partitions_Info.cc
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
		src/WinFsOps.cc
		src/WinNativeFS.cc
		src/WinLinuxFS.cc
		src/WinCapsText.cc
	)

	configure_file(resources/wparted.rc.in "${CMAKE_CURRENT_BINARY_DIR}/wparted.rc" @ONLY)
	configure_file(resources/wparted.exe.manifest.in "${CMAKE_CURRENT_BINARY_DIR}/wparted.exe.manifest" @ONLY)
	file(COPY resources/wparted.ico DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")

	add_executable(wparted WIN32 ${WPARTED_GUI_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/wparted.rc")
	target_include_directories(wparted PRIVATE
		"${CMAKE_CURRENT_BINARY_DIR}/include"
		"${CMAKE_CURRENT_SOURCE_DIR}/include/win32compat")
	target_compile_definitions(wparted PRIVATE HAVE_CONFIG_H=1)
	target_link_libraries(wparted PRIVATE wparted_disk PkgConfig::GTKMM)
	install(TARGETS wparted RUNTIME DESTINATION bin)

	# Translations (GParted's, the user interface is the same)
	find_program(MSGFMT_EXECUTABLE msgfmt)
	if(MSGFMT_EXECUTABLE)
		file(STRINGS po/LINGUAS WPARTED_LINGUAS REGEX "^[^#]")
		set(WPARTED_MO_FILES)
		foreach(lang IN LISTS WPARTED_LINGUAS)
			string(STRIP "${lang}" lang)
			set(mo "${CMAKE_CURRENT_BINARY_DIR}/share/locale/${lang}/LC_MESSAGES/wparted.mo")
			add_custom_command(OUTPUT "${mo}"
				COMMAND "${CMAKE_COMMAND}" -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/share/locale/${lang}/LC_MESSAGES"
				COMMAND "${MSGFMT_EXECUTABLE}" -o "${mo}" "${CMAKE_CURRENT_SOURCE_DIR}/po/${lang}.po"
				DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/po/${lang}.po"
				VERBATIM)
			list(APPEND WPARTED_MO_FILES "${mo}")
			install(FILES "${mo}" DESTINATION "share/locale/${lang}/LC_MESSAGES")
		endforeach()
		add_custom_target(wparted_translations ALL DEPENDS ${WPARTED_MO_FILES})
	else()
		message(WARNING "msgfmt not found: translations will not be built")
	endif()

	# Application icons for the GTK icon theme ("wparted")
	foreach(size 16 22 24 32 48)
		install(FILES "data/icons/hicolor_apps_${size}x${size}_wparted.png"
			DESTINATION "share/icons/hicolor/${size}x${size}/apps" RENAME wparted.png)
	endforeach()
	install(FILES data/icons/hicolor_apps_scalable_wparted.svg
		DESTINATION share/icons/hicolor/scalable/apps RENAME wparted.svg)
endif()


# ---------------------------------------------------------------------------
# Tests

if(WPARTED_BUILD_TESTS)
	enable_testing()
	find_package(GTest QUIET)
	if(NOT GTest_FOUND)
		add_library(wparted_gtest STATIC lib/gtest/src/gtest-all.cc)
		target_include_directories(wparted_gtest PUBLIC
			"${CMAKE_CURRENT_SOURCE_DIR}/lib/gtest/include"
			"${CMAKE_CURRENT_SOURCE_DIR}/lib/gtest")
		find_package(Threads REQUIRED)
		target_link_libraries(wparted_gtest PUBLIC Threads::Threads)
		add_library(wparted_gtest_main STATIC lib/gtest/src/gtest_main.cc)
		target_link_libraries(wparted_gtest_main PUBLIC wparted_gtest)
		add_library(GTest::gtest ALIAS wparted_gtest)
		add_library(GTest::gtest_main ALIAS wparted_gtest_main)
	endif()

	add_executable(wparted_unit_tests
		tests/MemDiskBackend.cc
		tests/test_WinDiskModel.cc
		tests/test_PartedBridge.cc
		tests/test_WinFsProbe.cc
		tests/test_WinBroker.cc
		tests/test_WinCapabilities.cc
		tests/test_WinFsUsage.cc
		tests/test_WinSwap.cc
		tests/test_WinHelpers.cc
	)
	target_include_directories(wparted_unit_tests PRIVATE tests)
	target_link_libraries(wparted_unit_tests PRIVATE wparted_disk wparted_broker_client GTest::gtest_main)
	add_test(NAME wparted_unit_tests COMMAND wparted_unit_tests)
	if(DEFINED ENV{WPARTED_TEST_IMAGES})
		set_tests_properties(wparted_unit_tests PROPERTIES ENVIRONMENT "WPARTED_TEST_IMAGES=$ENV{WPARTED_TEST_IMAGES}")
	endif()

	if(NOT WIN32)
		# Serves an image file through the broker to the e2fsprogs tools built by
		# helpers/e2fsprogs/build.sh (tests/test_e2fsprogs_broker.sh).
		find_package(Threads REQUIRED)
		add_executable(wparted_broker_harness tests/broker_harness.cc)
		target_link_libraries(wparted_broker_harness PRIVATE wparted_disk Threads::Threads)
	endif()

	if(WIN32)
		# Runs real partition table and file system operations, but only on the
		# virtual disk named by WPARTED_TEST_VHD_DISK (see tests/test_WinIntegration.cc).
		add_executable(wparted_win_tests tests/test_WinIntegration.cc)
		target_link_libraries(wparted_win_tests PRIVATE wparted_disk GTest::gtest_main)

		# Plays a compromised helper inside the sandbox (see the test above).
		add_executable(wparted-sandbox-probe helpers/test/sandbox_probe.c)
		target_link_libraries(wparted-sandbox-probe PRIVATE wparted_broker_client)
		target_link_options(wparted-sandbox-probe PRIVATE -static)
		add_dependencies(wparted_win_tests wparted-sandbox-probe)
	endif()
endif()
