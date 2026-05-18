cmake_minimum_required(VERSION 3.16)

# El código del firmware está dentro de Ships/
set(EXTRA_COMPONENT_DIRS Ships)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(SchedulingShips)