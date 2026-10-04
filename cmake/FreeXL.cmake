if(ENABLE_XLS)
  find_path(FREEXL_INCLUDE_DIR freexl.h REQUIRED)
  find_library(FREEXL_LIBRARY freexl REQUIRED)
  # Vcpkg's Windows FreeXL port is a shared library.  App-local deployment is
  # disabled in our reproducible build script, so copy the runtime explicitly
  # beside each executable.  This makes `build\\mvo.exe data.xls` runnable
  # from a clean shell without requiring the vcpkg bin directory on PATH.
  get_filename_component(FREEXL_LIBRARY_DIR "${FREEXL_LIBRARY}" DIRECTORY)
  get_filename_component(FREEXL_PREFIX "${FREEXL_LIBRARY_DIR}" DIRECTORY)
  find_file(FREEXL_RUNTIME freexl-1.dll
    PATHS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin"
          "${FREEXL_PREFIX}/bin"
          "${CMAKE_SOURCE_DIR}/.deps/bin"
    NO_DEFAULT_PATH)
  if(WIN32 AND FREEXL_RUNTIME)
    get_filename_component(FREEXL_RUNTIME_DIR "${FREEXL_RUNTIME}" DIRECTORY)
    set(MVO_XLS_RUNTIME_FILES "${FREEXL_RUNTIME}")
    # FreeXL 2.0 reads XLSX/ODS too. Its DLL also imports these dependencies;
    # minizip in turn imports z.dll. Older FreeXL builds may omit some of them.
    foreach(runtime_name libexpat.dll minizip.dll iconv-2.dll z.dll)
      if(EXISTS "${FREEXL_RUNTIME_DIR}/${runtime_name}")
        list(APPEND MVO_XLS_RUNTIME_FILES "${FREEXL_RUNTIME_DIR}/${runtime_name}")
      endif()
    endforeach()
  elseif(WIN32)
    message(WARNING "FreeXL runtime DLL was not found. A shared FreeXL build requires its DLLs beside mvo.exe or on PATH.")
  endif()
  target_include_directories(mvo_core SYSTEM PRIVATE "${FREEXL_INCLUDE_DIR}")
  target_link_libraries(mvo_core PRIVATE "${FREEXL_LIBRARY}")
  target_compile_definitions(mvo_core PRIVATE ENABLE_XLS)
endif()
