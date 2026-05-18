# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "/home/andre/esp/esp-idf/components/bootloader/subproject"
  "/home/andre/Desktop/Proyecto1_Scheduling_Ships/Scheduling_Ships/build/bootloader"
  "/home/andre/Desktop/Proyecto1_Scheduling_Ships/Scheduling_Ships/build/bootloader-prefix"
  "/home/andre/Desktop/Proyecto1_Scheduling_Ships/Scheduling_Ships/build/bootloader-prefix/tmp"
  "/home/andre/Desktop/Proyecto1_Scheduling_Ships/Scheduling_Ships/build/bootloader-prefix/src/bootloader-stamp"
  "/home/andre/Desktop/Proyecto1_Scheduling_Ships/Scheduling_Ships/build/bootloader-prefix/src"
  "/home/andre/Desktop/Proyecto1_Scheduling_Ships/Scheduling_Ships/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/andre/Desktop/Proyecto1_Scheduling_Ships/Scheduling_Ships/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/andre/Desktop/Proyecto1_Scheduling_Ships/Scheduling_Ships/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
