cmake_minimum_required(VERSION 2.8)
add_executable(rastertotmis
	filter/TmImpactSlip.c
)
target_link_libraries(rastertotmis cupsimage cups)
