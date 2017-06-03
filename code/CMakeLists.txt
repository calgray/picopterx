#picopter cmake configuration file

cmake_minimum_required (VERSION 2.8)
project (picopter)

# Text colours
# http://stackoverflow.com/questions/18968979/how-to-get-colorized-output-with-cmake
if(NOT WIN32)
	string (ASCII 27 Esc)
	set (ColourReset "${Esc}[m")
	set (ColourBold  "${Esc}[1m")
	set (Red         "${Esc}[31m")
	set (Green       "${Esc}[32m")
	set (Yellow      "${Esc}[33m")
endif()

#Includes
include (CheckCSourceCompiles)
include (CheckLibraryExists) 
include (FindPkgConfig)
include (ExternalProject)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake_modules/")

#Versioning
execute_process (COMMAND git rev-parse HEAD 
				 OUTPUT_STRIP_TRAILING_WHITESPACE
				 OUTPUT_VARIABLE PICOPTER_VERSION
)
execute_process (COMMAND git  show -s --format=%ci HEAD
				 OUTPUT_STRIP_TRAILING_WHITESPACE
				 OUTPUT_VARIABLE PICOPTER_DATE
)

#==============================
# Platform Detection
#=============================
if (WIN32 AND NOT CYGWIN)
	message (WARNING "${Red}Windows currently no supported. POSIX C code used in base, pkg-config used to find thrift+opencv. ${ColourReset}")
	set(DISABLE_SERVER True)
elseif (CYGWIN)
	message (WARNING "${Green}Cygwin is supported. Make sure to manually build thrift and enable _GNU_SOURCE${ColourReset}")
elseif (APPLE)
	message (WARNING "${Yellow}OS X is unsupported. Untested.${ColourReset}")
endif()

#Enable all POSIX features (Cygwin requires this flag, see features.h)
if(CYGWIN)
	add_definitions(-D_GNU_SOURCE)
endif()

#=============================
# Raspberry Pi Detection
# https://raspberrypi.stackexchange.com/questions/754/how-can-i-detect-that-im-compiling-for-raspberry-pi
# consider cross-compiling, pi running OS othere than Raspbian
if (NOT FORCE_EMULATION)
	#Check if we're building on the pi
	CHECK_C_SOURCE_COMPILES ("
		#include <stdio.h>
		#ifndef __arm__
		#error Not using ARM compiler
		#endif
		int main(){return 0;}" 
		RASPBERRY_PI)
endif()

#Alternative method would be more reliable, especially if building on a different ARM board
#if(EXISTS /opt/vc/src/hello_pi)
#set(RASPBERRY_PI True)
#endif()

#==================================
# Folder Setup
# Rename these, PI is irrelevant
#Set src and include folders
set (PI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set (PI_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set (PI_EMULATE ${CMAKE_CURRENT_SOURCE_DIR}/emulation)
set (PI_DOC ${CMAKE_CURRENT_SOURCE_DIR}/doc)
set (PI_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

#Set the output folder
set (CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set (CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set (CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)


#===========================
# Libraries
#=========================
message("Searching for Libraries:")

# MAVLink (Header only)
include_directories (${PI_THIRD_PARTY}/mavlink)

#===================================
# OpenCV (used in base)
pkg_check_modules (LIBOPENCV REQUIRED opencv)
if(NOT LIBOPENCV_FOUND)
	message(STATUS "${Red}OpenCV Missing${ColourReset}")
else()
	message(STATUS "${Green}OpenCV Found${ColourReset}")
	include_directories (${LIBOPENCV_INCLUDE_DIRS})
	link_directories (${LIBOPENCV_LIBRARY_DIRS})
endif()

#===================================
# RapidJSON (Header only)
include_directories (${PI_THIRD_PARTY}/rapidjson)

#==================================
#Wiring Pi and Wiring Emulation
if (NOT RASPBERRY_PI)
	include_directories (${PI_EMULATE})
	message (STATUS "${Yellow}Using WiringPi emulation - ${ColourBold}remember to check on the RPi!${ColourReset}")
else ()
	CHECK_LIBRARY_EXISTS (wiringPi digitalWrite "" WIRINGPI_FOUND)
	if (NOT WIRINGPI_FOUND)
		message (FATAL_ERROR "${Red}Building on the RPi without wiringPi${ColourReset}")
	endif (NOT WIRINGPI_FOUND)
endif ()

#======================
# Raspberry Pi Video Core
if(RASPBERRY_PI)
link_directories (/opt/vc/lib /opt/vc/src/hello_pi/libs/ilclient)
endif()

#=======================
# OMXCV - Raspberry Pi 
if(NOT RASPBERRY_PI)
	message(STATUS "${Yellow}Skipping OMXCV${ColourReset}")
else()
	message(STATUS "${Green}OMXCV Found${ColourReset}")
	add_subdirectory (${PI_THIRD_PARTY}/omxcv)
	include_directories (${omxcv_SOURCE_DIR})
endif()

#==============================
# LibGPS and LibGPS Emulation
pkg_check_modules (LIBGPS libgps QUIET)
if(NOT LIBGPS_FOUND)
	message (STATUS "${Yellow}Emulating libgps - beware bogus values!${ColourReset}")
	include_directories(${PI_EMULATE}/gps-emu)
else()
	message(STATUS "${Green}LibGPS Found${ColourReset}")
	include_directories (${LIBGPS_INCLUDE_DIRS})
	link_directories (${LIBGPS_LIBRARY_DIRS})
	set (IS_REAL_LIBGPS ${LIBGPS_FOUND}) #TODO: u serious dude?
endif()

#Australian tuning parameters
#set (IS_AUSTRALIAN_TUNED ON CACHE BOOL "Enable Australia-specific tuning (default: on)")

#===============================
# Thrift (for server only?)
# check these links for building in cygwin
#http://fantasticsmiley.blogspot.com.au/2014/09/build-apachethrift-under-cygwin.html
#https://wiki.apache.org/thrift/ThriftInstallationWin32
#If thrift does not install config-pkg files..:
#https://github.com/facebook/fbthrift/blob/master/thrift/lib/cpp/thrift.pc
#Find OpenCV
#https://github.com/feixh/openFABMAP/blob/master/FindOpenCV.cmake

# Thrift binary 
find_program (THRIFT_COMPILER thrift DOC "Thrift compiler executable")
if (NOT THRIFT_COMPILER)
	message (FATAL_ERROR "${Red}Thrift Compiler not found. Please install it.${ColourReset}")
else()
	message(STATUS "${Green}Thrift Compiler Found${ColourReset}")
endif()

#Thrift libs
pkg_check_modules (LIBTHRIFT REQUIRED thrift QUIET)
if(NOT LIBTHRIFT_FOUND)
	message(STATUS "${Red}LibThrift Missing${ColourReset}")
else()
	message(STATUS "${Green}LibThrift Found${ColourReset}")
	include_directories (${LIBTHRIFT_INCLUDE_DIRS})
	link_directories (${LIBTHRIFT_LIBRARY_DIRS})
endif()

#=====================================
# Boost (for thrift/web server only)
find_package (Boost REQUIRED QUIET) # at least 1.53.0
if(NOT Boost_FOUND)
	message(STATUS "${Red}Boost Missing${ColourReset}")
else()
	message(STATUS "${Green}Boost Found${ColourReset}")
	include_directories (${Boost_INCLUDE_DIRS})
endif()

#========================================
#Optional libraries (tcmalloc; from google perftools)
CHECK_LIBRARY_EXISTS(tcmalloc malloc "" LIBTCMALLOC_FOUND)
CHECK_LIBRARY_EXISTS(profiler malloc "" LIBPROFILER_FOUND)

if (LIBTCMALLOC_FOUND AND LIBPROFILER_FOUND)
	#We want to link tcmalloc and the profiler to *everything*
	message(STATUS "${Green}LibTCMallao Found${ColourReset}")
	message(STATUS "${Green}LibProfiler Found${ColourReset}")
	macro (add_executable _name)
		# invoke built-in add_executable
		_add_executable(${ARGV})
		if (TARGET ${_name})
			target_link_libraries(${_name} LINK_PRIVATE tcmalloc profiler)
		endif()
	endmacro()
else ()
	message (WARNING "${Yellow}Google perftools (tcmalloc/profiler) was not found. Installation is strongly recommended!${ColourReset}")
endif()

#==========================================
# Doxygen Documentation + LaTeX (optional)
set_directory_properties (PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES ${PI_DOC})
find_package (Doxygen QUIET)
if (NOT DOXYGEN_FOUND)
	message (STATUS "${Yellow}Doxygen Missing, Skipping Latex.${ColourReset}")
else()
	message (STATUS "${Green}Doxygen Found${ColourReset}")
	
	find_package (latex QUIET)
	if (LATEX_COMPILER)
		add_custom_target (doc
			${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			COMMAND make -C "${PI_DOC}/latex"
			COMMENT "Generating code documentation..." VERBATIM
		)
	else()
		add_custom_target (doc
			${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
			COMMENT "Generating code documentation..." VERBATIM
	)
	endif()
endif()



#============================================
#Set the include folders for the compiler
include_directories (${PI_INCLUDE})

#Set compiler flags
set (GCC_COMPILE_FLAGS "-std=c++11 -Wall -pedantic -g")
set (GCC_LINK_FLAGS "-pthread")
set (CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GCC_COMPILE_FLAGS}")
set (CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_LINK_FLAGS}")

#Pretty filenames (see log.h) and log filepath
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__FILENAME__='\"$(subst ${CMAKE_SOURCE_DIR}/src/,,$(abspath $<))\"'")

set (PICOPTER_HOME_LOCATION $ENV{HOME})
if(WIN32)
string(REPLACE "\\" "/" PICOPTER_HOME_LOCATION ${PICOPTER_HOME_LOCATION})
endif()

message("Home : ${PICOPTER_HOME_LOCATION}")

if (NOT PICOPTER_HOME_LOCATION)
	set (PICOPTER_HOME_LOCATION "/home/pi")
endif()

#Generate the config file
if (UNIX AND NOT CYGWIN)
	set (USE_SYSLOG 1)
endif()
configure_file (${PI_INCLUDE}/config.h.in ${PI_INCLUDE}/config.h)


#Setup the testing framework (gtest)
if (test)
	add_subdirectory (${PI_THIRD_PARTY}/googletest)
	enable_testing ()
	include_directories (${gtest_SOURCE_DIR}/include ${gtest_SOURCE_DIR})
	add_subdirectory (${CMAKE_CURRENT_SOURCE_DIR}/test)
endif()

#===================
# Check Libraries
#===================
if(NOT LIBOPENCV_FOUND)
meesage(ERROR "${Yellow}OpenCV not found, Base requires it.${ColourReset}")
endif()

if(NOT Boost_FOUND)
message(WARNING "${Yellow}Boost not found, disabling server.${ColourReset}")
set(DISABLE_SERVER True)
endif()

if(NOT LIBTHRIFT_FOUND)
message(WARNING "${Yellow}Thrift not found, disabling server.${ColourReset}")
set(DISABLE_SERVER True)
endif()

#=====================
# Display Target List
message("Building:")
message(STATUS "${Green}Base : True${ColourReset}")
message(STATUS "${Green}Modules : True${ColourReset}")
message(STATUS "${Green}Apps : True${ColourReset}")
message(STATUS "${Green}Server : True${ColourReset}")
message("")

#=================================
# Project SubModules
#=================================
#Add the base module
add_subdirectory (${PI_SRC}/base)

#Add the modules folder
if(LIBOPENCV_FOUND)
	add_subdirectory (${PI_SRC}/modules)
endif()

#Add the testing folder
add_subdirectory (${PI_SRC}/apps)


#Add server executable
if (NOT DISABLE_SERVER)
	add_subdirectory (${PI_SRC}/server)
endif ()
