cmake_minimum_required(VERSION 2.8)
add_executable(rastertotmtr
	filter/TmThermalReceipt.c
)
target_link_libraries(rastertotmtr cupsimage cups)
