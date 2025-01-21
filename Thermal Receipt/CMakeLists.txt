cmake_minimum_required(VERSION 2.8)

add_executable(rastertotmtr
	filter/TmThermalReceipt.c
)

find_path(CUPS_INCLUDE_DIR NAMES cups/ppd.h PATHS /usr/local/include)
find_library(CUPS_LIBRARY NAMES cups PATHS /usr/local/lib)

if(CUPS_INCLUDE_DIR AND CUPS_LIBRARY)
    include_directories(${CUPS_INCLUDE_DIR})
    target_link_libraries(rastertotmtr ${CUPS_LIBRARY})
else()
    message(FATAL_ERROR "CUPS not found. Install CUPS development files.")
endif()

target_link_libraries(rastertotmtr)