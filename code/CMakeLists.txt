#picopter cmake configuration file

cmake_minimum_required (VERSION 2.8)
project (picopter)

#Text colours
string (ASCII 27 Esc)
set (ColourReset "${Esc}[m")
set (ColourBold  "${Esc}[1m")
set (Red         "${Esc}[31m")
set (Green       "${Esc}[32m")
set (Yellow      "${Esc}[33m")

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

#OS checks
if (WIN32 AND NOT CYGWIN)
	message (WARNING "${Red}Windows is partially supported. POSIX C code used in base, pkg-config used to find thrift+opencv. ${ColourReset}")
elseif (CYGWIN)
	message (WARNING "${Green}Cygwin is supported. Must manually build thrift, ensure _GNU_SOURCE is enabled. ${ColourReset}")
elseif (APPLE)
	message (WARNING "${Yellow}OS X is unsupported. Untested.${ColourReset}")
endif()

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

#Include the MAVLink headers
include_directories (${PI_THIRD_PARTY}/mavlink)

#OpenCV Library
pkg_check_modules (LIBOPENCV REQUIRED opencv)
include_directories (${LIBOPENCV_INCLUDE_DIRS})
link_directories (${LIBOPENCV_LIBRARY_DIRS})

#Check for gpsd
pkg_check_modules (LIBGPS libgps)
include_directories (${LIBGPS_INCLUDE_DIRS})
link_directories (${LIBGPS_LIBRARY_DIRS})
set (IS_REAL_LIBGPS ${LIBGPS_FOUND})

#Optional libraries (tcmalloc; from google perftools)
#CHECK_LIBRARY_EXISTS(tcmalloc malloc "" HAVE_LIBTCMALLOC)
#CHECK_LIBRARY_EXISTS(profiler malloc "" HAVE_LIBPROFILER)

if (HAVE_LIBTCMALLOC AND HAVE_LIBPROFILER)
	#We want to link tcmalloc and the profiler to *everything*
	macro (add_executable _name)
		# invoke built-in add_executable
		_add_executable(${ARGV})
		if (TARGET ${_name})
			target_link_libraries(${_name} LINK_PRIVATE tcmalloc profiler)
		endif()
	endmacro()
else (HAVE_LIBTCMALLOC AND HAVE_LIBPROFILER)
	message (WARNING "${Yellow}Google perftools (tcmalloc/profiler) was not found. Installation is strongly recommended!${ColourReset}")
endif (HAVE_LIBTCMALLOC AND HAVE_LIBPROFILER)

#=============================================
# RapidJSON (Header only)
include_directories (${PI_THIRD_PARTY}/rapidjson)

#==========================================
# Doxygen Documentation + LaTeX (optional)
set_directory_properties (PROPERTIES ADDITIONAL_MAKE_CLEAN_FILES ${PI_DOC})
find_package (Doxygen QUIET)
find_package (latex QUIET)
if (DOXYGEN_FOUND)
	if (LATEX_COMPILER)
	add_custom_target (doc
		${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMAND make -C "${PI_DOC}/latex"
		COMMENT "Generating code documentation..." VERBATIM
	)
	else(LATEX_COMPILER)
	add_custom_target (doc
		${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Doxyfile
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
		COMMENT "Generating code documentation..." VERBATIM
	)
	endif(LATEX_COMPILER)
endif(DOXYGEN_FOUND)

if (NOT FORCE_EMULATION)
	#Check if we're building on the pi
	CHECK_C_SOURCE_COMPILES ("
		#include <stdio.h>
		#ifndef __arm__
		#error Not building on the pi
		#endif
		int main(){return 0;}
	" IS_ON_PI)
endif()

#==================================
#Wiring Pi and Wiring Emulation
if (NOT IS_ON_PI)
	include_directories (${PI_EMULATE})
	message (WARNING "${Yellow}Using the emulation files - ${ColourBold}remember to check on the RPi!${ColourReset}")
else ()
	CHECK_LIBRARY_EXISTS (wiringPi digitalWrite "" WIRINGPI_FOUND)
	if (NOT WIRINGPI_FOUND)
		message (FATAL_ERROR "${Red}Building on the RPi without wiringPi${ColourReset}")
	endif (NOT WIRINGPI_FOUND)
	add_subdirectory (${PI_THIRD_PARTY}/omxcv)
	include_directories (${omxcv_SOURCE_DIR})
	link_directories (/opt/vc/lib /opt/vc/src/hello_pi/libs/ilclient)
endif ()


#================================
# LibGPS and LibGPS Emulation
if (NOT LIBGPS_FOUND)
	message (WARNING "${Yellow}Emulating libgps - beware bogus values!${ColourReset}")
	include_directories(${PI_EMULATE}/gps-emu)
endif()

#Australian tuning parameters
#set (IS_AUSTRALIAN_TUNED ON CACHE BOOL "Enable Australia-specific tuning (default: on)")




#Set the include folders for the compiler
include_directories (${PI_INCLUDE})

#Enable all POSIX features (Cygwin requires this flag, see features.h)
add_definitions(-D_GNU_SOURCE)

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

#=================================
# Project SubModules
#=================================

#Add the base module
add_subdirectory (${PI_SRC}/base)

#Add the modules folder
add_subdirectory (${PI_SRC}/modules)

#Add the testing folder
add_subdirectory (${PI_SRC}/apps)

#Add in the server executable
if (NOT DISABLE_SERVER)
	add_subdirectory (${PI_SRC}/server)
endif (NOT DISABLE_SERVER)
