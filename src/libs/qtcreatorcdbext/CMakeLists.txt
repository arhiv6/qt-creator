cmake_minimum_required(VERSION 3.16)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake")

project(qtcreatorcdbext)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (NOT QT_CREATOR_API_DEFINED)
  # standalone build
  include(QtCreatorIDEBranding)
  include(QtCreatorAPI)
endif()

if (NOT WIN32 OR NOT MSVC)
  return()
endif()

find_library(DbgEngLib dbgeng)

set(ArchSuffix "32")
if (CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(ArchSuffix "64")
endif()

if (MSVC_CXX_ARCHITECTURE_ID MATCHES "^ARM")
  set(ArchSuffix "arm${ArchSuffix}")
endif()

add_qtc_library(qtcreatorcdbext SHARED
  COMPONENT qtcreatorcdbext
  DEPENDS ${DbgEngLib}
  DESTINATION lib/qtcreatorcdbext${ArchSuffix}/
  SOURCES
    common.cpp common.h
    containers.cpp containers.h
    eventcallback.cpp eventcallback.h
    extensioncontext.cpp extensioncontext.h
    gdbmihelpers.cpp gdbmihelpers.h
    iinterfacepointer.h
    knowntype.h
    outputcallback.cpp outputcallback.h
    qtcreatorcdbext.def
    qtcreatorcdbextension.cpp
    stringutils.cpp stringutils.h
    symbolgroup.cpp symbolgroup.h
    symbolgroupnode.cpp symbolgroupnode.h
    symbolgroupvalue.cpp symbolgroupvalue.h
)

qtc_library_enabled(_library_enabled qtcreatorcdbext)
if (_library_enabled)
  # statically link MSVC runtime
  set_property(TARGET qtcreatorcdbext PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(qtcreatorcdbext PUBLIC /EHsc)

  find_package(Python3 3.8 COMPONENTS Development)

  if (NOT ${Python3_Development_FOUND})
    message(WARNING "PythonLibs (at least version 3.8) not found. qtcreatorcdbext will be built without Python support.")
    return()
  endif()

  set(PythonRegex "^(.*)/(.*)/(python([0-9]+))${CMAKE_IMPORT_LIBRARY_SUFFIX}$")
  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(PythonRegex "^(.*)/(.*)/(python([0-9]+)_d)${CMAKE_IMPORT_LIBRARY_SUFFIX}$")
  endif()

  foreach(lib IN LISTS Python3_LIBRARIES)
    if (lib MATCHES ${PythonRegex})
      if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(PythonZipFileName "python${CMAKE_MATCH_4}_d.zip")
      else()
        set(PythonZipFileName "python${CMAKE_MATCH_4}.zip")
      endif()

      set(PythonDll "${CMAKE_MATCH_1}/${CMAKE_MATCH_3}${CMAKE_SHARED_LIBRARY_SUFFIX}")
      set(PythonExe "${CMAKE_MATCH_1}/python${CMAKE_EXECUTABLE_SUFFIX}")
      set(PythonZip "${CMAKE_MATCH_1}/${PythonZipFileName}")

      break()
    endif()
  endforeach()

  if (NOT PythonDll)
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
      message(WARNING "The Debug build of Qt Creator requires Debug Python libraries. Please check your Python installation")
    endif()
    message(WARNING "PythonDll not found. qtcreatorcdbext will be built without Python support.")
    return()
  endif()

  extend_qtc_library(qtcreatorcdbext
    DEPENDS "${Python3_LIBRARIES}"
    INCLUDES "${Python3_INCLUDE_DIRS}"
    DEFINES WITH_PYTHON=1 PY_SSIZE_T_CLEAN
    SOURCES
      pycdbextmodule.cpp pycdbextmodule.h
      pyfield.cpp pyfield.h
      pystdoutredirect.cpp pystdoutredirect.h
      pytype.cpp pytype.h
      pyvalue.cpp pyvalue.h
  )

  if (NOT EXISTS "${PythonZip}" AND
      NOT EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${PythonZipFileName}")
    include(CreatePythonXY)
    create_python_xy("${PythonExe}" "${CMAKE_CURRENT_BINARY_DIR}/${PythonZipFileName}")
  endif()

  if (NOT EXISTS "${PythonZip}" AND
      EXISTS "${CMAKE_CURRENT_BINARY_DIR}/${PythonZipFileName}")
    set(PythonZip "${CMAKE_CURRENT_BINARY_DIR}/${PythonZipFileName}")
  endif()

  list(APPEND deployPythonFiles "${PythonDll}")
  list(APPEND deployPythonFiles "${PythonZip}")

  install(FILES ${deployPythonFiles}
          DESTINATION lib/qtcreatorcdbext${ArchSuffix}/
          COMPONENT qtcreatorcdbext)

  add_custom_target(copy_python_dll ALL VERBATIM)

  qtc_output_binary_dir(output_binary_dir)
  add_custom_command(TARGET copy_python_dll POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${deployPythonFiles} "${output_binary_dir}/lib/qtcreatorcdbext${ArchSuffix}/"
    VERBATIM
  )
endif()
