# Modified version by A.Doronin

# Example:
# include(conan2.cmake)
# set(CONAN2_REMOTE xxx)
# set(CONAN2_REMOTE_URL yyy)
# #may be provided in ENV: CONAN_LOGIN_USERNAME
# #may be provided in ENV: CONAN_PASSWORD
# conan2_install()

# The MIT License (MIT)
#
# Copyright (c) 2024 JFrog
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

set(CONAN_MINIMUM_VERSION 2.0.5)

# Create a new policy scope and set the minimum required cmake version so the
# features behind a policy setting like if(... IN_LIST ...) behaves as expected
# even if the parent project does not specify a minimum cmake version or a minimum
# version less than this module requires (e.g. 3.0) before the first project() call.
# (see: https://cmake.org/cmake/help/latest/variable/CMAKE_PROJECT_TOP_LEVEL_INCLUDES.html)
#
# The policy-affecting calls like cmake_policy(SET...) or `cmake_minimum_required` only
# affects the current policy scope, i.e. between the PUSH and POP in this case.
#
# https://cmake.org/cmake/help/book/mastering-cmake/chapter/Policies.html#the-policy-stack
cmake_policy(PUSH)
cmake_minimum_required(VERSION 3.19...3.24)


function(_detect_os os os_api_level os_sdk os_subsystem os_version)
    # it could be cross compilation
    message(STATUS "CMake-Conan: cmake_system_name=${CMAKE_SYSTEM_NAME}")
    if(CMAKE_SYSTEM_NAME AND NOT CMAKE_SYSTEM_NAME STREQUAL "Generic")
        if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
            set(${os} Macos PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "QNX")
            set(${os} Neutrino PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_NAME STREQUAL "CYGWIN")
            set(${os} Windows PARENT_SCOPE)
            set(${os_subsystem} cygwin PARENT_SCOPE)
        elseif(CMAKE_SYSTEM_NAME MATCHES "^MSYS")
            set(${os} Windows PARENT_SCOPE)
            set(${os_subsystem} msys2 PARENT_SCOPE)
        else()
            set(${os} ${CMAKE_SYSTEM_NAME} PARENT_SCOPE)
        endif()
        if(CMAKE_SYSTEM_NAME STREQUAL "Android")
            if(DEFINED ANDROID_PLATFORM)
                string(REGEX MATCH "[0-9]+" _os_api_level ${ANDROID_PLATFORM})
            elseif(DEFINED CMAKE_SYSTEM_VERSION)
                set(_os_api_level ${CMAKE_SYSTEM_VERSION})
            endif()
            message(STATUS "CMake-Conan: android api level=${_os_api_level}")
            set(${os_api_level} ${_os_api_level} PARENT_SCOPE)
        endif()
        if(CMAKE_SYSTEM_NAME MATCHES "Darwin|iOS|tvOS|watchOS")
            # CMAKE_OSX_SYSROOT contains the full path to the SDK for MakeFile/Ninja
            # generators, but just has the original input string for Xcode.
            if(NOT IS_DIRECTORY ${CMAKE_OSX_SYSROOT})
                set(_os_sdk ${CMAKE_OSX_SYSROOT})
            else()
                if(CMAKE_OSX_SYSROOT MATCHES Simulator)
                    set(apple_platform_suffix simulator)
                else()
                    set(apple_platform_suffix os)
                endif()
                if(CMAKE_OSX_SYSROOT MATCHES AppleTV)
                    set(_os_sdk "appletv${apple_platform_suffix}")
                elseif(CMAKE_OSX_SYSROOT MATCHES iPhone)
                    set(_os_sdk "iphone${apple_platform_suffix}")
                elseif(CMAKE_OSX_SYSROOT MATCHES Watch)
                    set(_os_sdk "watch${apple_platform_suffix}")
                endif()
            endif()
            if(DEFINED os_sdk)
                message(STATUS "CMake-Conan: cmake_osx_sysroot=${CMAKE_OSX_SYSROOT}")
                set(${os_sdk} ${_os_sdk} PARENT_SCOPE)
            endif()
            if(DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
                message(STATUS "CMake-Conan: cmake_osx_deployment_target=${CMAKE_OSX_DEPLOYMENT_TARGET}")
                set(${os_version} ${CMAKE_OSX_DEPLOYMENT_TARGET} PARENT_SCOPE)
            endif()
        endif()
    endif()
endfunction()


function(_detect_arch arch)
    # CMAKE_OSX_ARCHITECTURES can contain multiple architectures, but Conan only supports one.
    # Therefore this code only finds one. If the recipes support multiple architectures, the
    # build will work. Otherwise, there will be a linker error for the missing architecture(s).
    if(DEFINED CMAKE_OSX_ARCHITECTURES)
        string(REPLACE " " ";" apple_arch_list "${CMAKE_OSX_ARCHITECTURES}")
        list(LENGTH apple_arch_list apple_arch_count)
        if(apple_arch_count GREATER 1)
            message(WARNING "CMake-Conan: Multiple architectures detected, this will only work if Conan recipe(s) produce fat binaries.")
        endif()
    endif()
    if(CMAKE_SYSTEM_NAME MATCHES "Darwin|iOS|tvOS|watchOS" AND NOT CMAKE_OSX_ARCHITECTURES STREQUAL "")
        set(host_arch ${CMAKE_OSX_ARCHITECTURES})
    elseif(MSVC)
        set(host_arch ${CMAKE_CXX_COMPILER_ARCHITECTURE_ID})
    else()
        set(host_arch ${CMAKE_SYSTEM_PROCESSOR})
    endif()
    if(host_arch MATCHES "aarch64|arm64|ARM64")
        set(_arch armv8)
    elseif(host_arch MATCHES "armv7|armv7-a|armv7l|ARMV7")
        set(_arch armv7)
    elseif(host_arch MATCHES armv7s)
        set(_arch armv7s)
    elseif(host_arch MATCHES "i686|i386|X86")
        set(_arch x86)
    elseif(host_arch MATCHES "AMD64|amd64|x86_64|x64")
        set(_arch x86_64)
    endif()
    message(STATUS "CMake-Conan: cmake_system_processor=${_arch}")
    set(${arch} ${_arch} PARENT_SCOPE)
endfunction()


function(_detect_cxx_standard cxx_standard)
    set(${cxx_standard} ${CMAKE_CXX_STANDARD} PARENT_SCOPE)
    if(CMAKE_CXX_EXTENSIONS)
        set(${cxx_standard} "gnu${CMAKE_CXX_STANDARD}" PARENT_SCOPE)
    endif()
endfunction()


macro(_detect_gnu_libstdcxx)
    # _conan_is_gnu_libstdcxx true if GNU libstdc++
    check_cxx_source_compiles("
    #include <cstddef>
    #if !defined(__GLIBCXX__) && !defined(__GLIBCPP__)
    static_assert(false);
    #endif
    int main(){}" _conan_is_gnu_libstdcxx)

    # _conan_gnu_libstdcxx_is_cxx11_abi true if C++11 ABI
    check_cxx_source_compiles("
    #include <string>
    static_assert(sizeof(std::string) != sizeof(void*), \"using libstdc++\");
    int main () {}" _conan_gnu_libstdcxx_is_cxx11_abi)

    set(_conan_gnu_libstdcxx_suffix "")
    if(_conan_gnu_libstdcxx_is_cxx11_abi)
        set(_conan_gnu_libstdcxx_suffix "11")
    endif()
    unset (_conan_gnu_libstdcxx_is_cxx11_abi)
endmacro()


macro(_detect_libcxx)
    # _conan_is_libcxx true if LLVM libc++
    check_cxx_source_compiles("
    #include <cstddef>
    #if !defined(_LIBCPP_VERSION)
       static_assert(false);
    #endif
    int main(){}" _conan_is_libcxx)
endmacro()


function(_detect_lib_cxx lib_cxx)
    if(CMAKE_SYSTEM_NAME STREQUAL "Android")
        message(STATUS "CMake-Conan: android_stl=${CMAKE_ANDROID_STL_TYPE}")
        set(${lib_cxx} ${CMAKE_ANDROID_STL_TYPE} PARENT_SCOPE)
        return()
    endif()

    include(CheckCXXSourceCompiles)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        _detect_gnu_libstdcxx()
        set(${lib_cxx} "libstdc++${_conan_gnu_libstdcxx_suffix}" PARENT_SCOPE)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "AppleClang")
        set(${lib_cxx} "libc++" PARENT_SCOPE)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND NOT CMAKE_SYSTEM_NAME MATCHES "Windows")
        # Check for libc++
        _detect_libcxx()
        if(_conan_is_libcxx)
            set(${lib_cxx} "libc++" PARENT_SCOPE)
            return()
        endif()

        # Check for libstdc++
        _detect_gnu_libstdcxx()
        if(_conan_is_gnu_libstdcxx)
            set(${lib_cxx} "libstdc++${_conan_gnu_libstdcxx_suffix}" PARENT_SCOPE)
            return()
        endif()

        # TODO: it would be an error if we reach this point
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        # Do nothing - compiler.runtime and compiler.runtime_type
        # should be handled separately: https://github.com/conan-io/cmake-conan/pull/516
        return()
    else()
        # TODO: unable to determine, ask user to provide a full profile file instead
    endif()
endfunction()


function(_detect_compiler compiler compiler_version compiler_runtime compiler_runtime_type)
    if(DEFINED CMAKE_CXX_COMPILER_ID)
        set(_compiler ${CMAKE_CXX_COMPILER_ID})
        set(_compiler_version ${CMAKE_CXX_COMPILER_VERSION})
    else()
        if(NOT DEFINED CMAKE_C_COMPILER_ID)
            message(FATAL_ERROR "C or C++ compiler not defined")
        endif()
        set(_compiler ${CMAKE_C_COMPILER_ID})
        set(_compiler_version ${CMAKE_C_COMPILER_VERSION})
    endif()

    message(STATUS "CMake-Conan: CMake compiler=${_compiler}")
    message(STATUS "CMake-Conan: CMake compiler version=${_compiler_version}")

    if(_compiler MATCHES MSVC)
        set(_compiler "msvc")
        string(SUBSTRING ${MSVC_VERSION} 0 3 _compiler_version)
        # Configure compiler.runtime and compiler.runtime_type settings for MSVC
        if(CMAKE_MSVC_RUNTIME_LIBRARY)
            set(_msvc_runtime_library ${CMAKE_MSVC_RUNTIME_LIBRARY})
        else()
            set(_msvc_runtime_library MultiThreaded$<$<CONFIG:Debug>:Debug>DLL) # default value documented by CMake
        endif()

        set(_KNOWN_MSVC_RUNTIME_VALUES "")
        list(APPEND _KNOWN_MSVC_RUNTIME_VALUES MultiThreaded MultiThreadedDLL)
        list(APPEND _KNOWN_MSVC_RUNTIME_VALUES MultiThreadedDebug MultiThreadedDebugDLL)
        list(APPEND _KNOWN_MSVC_RUNTIME_VALUES MultiThreaded$<$<CONFIG:Debug>:Debug> MultiThreaded$<$<CONFIG:Debug>:Debug>DLL)

        # only accept the 6 possible values, otherwise we don't don't know to map this
        if(NOT _msvc_runtime_library IN_LIST _KNOWN_MSVC_RUNTIME_VALUES)
            message(FATAL_ERROR "CMake-Conan: unable to map MSVC runtime: ${_msvc_runtime_library} to Conan settings")
        endif()

        # Runtime is "dynamic" in all cases if it ends in DLL
        if(_msvc_runtime_library MATCHES ".*DLL$")
            set(_compiler_runtime "dynamic")
        else()
            set(_compiler_runtime "static")
        endif()
        message(STATUS "CMake-Conan: CMake compiler.runtime=${_compiler_runtime}")

        # Only define compiler.runtime_type when explicitly requested
        # If a generator expression is used, let Conan handle it conditional on build_type
        if(NOT _msvc_runtime_library MATCHES "<CONFIG:Debug>:Debug>")
            if(_msvc_runtime_library MATCHES "Debug")
                set(_compiler_runtime_type "Debug")
            else()
                set(_compiler_runtime_type "Release")
            endif()
            message(STATUS "CMake-Conan: CMake compiler.runtime_type=${_compiler_runtime_type}")
        endif()

        unset(_KNOWN_MSVC_RUNTIME_VALUES)

    elseif(_compiler MATCHES AppleClang)
        set(_compiler "apple-clang")
        string(REPLACE "." ";" VERSION_LIST ${CMAKE_CXX_COMPILER_VERSION})
        list(GET VERSION_LIST 0 _compiler_version)
    elseif(_compiler MATCHES Clang)
        set(_compiler "clang")
        string(REPLACE "." ";" VERSION_LIST ${CMAKE_CXX_COMPILER_VERSION})
        list(GET VERSION_LIST 0 _compiler_version)
    elseif(_compiler MATCHES GNU)
        set(_compiler "gcc")
        string(REPLACE "." ";" VERSION_LIST ${CMAKE_CXX_COMPILER_VERSION})
        list(GET VERSION_LIST 0 _compiler_version)
    endif()

    message(STATUS "CMake-Conan: [settings] compiler=${_compiler}")
    message(STATUS "CMake-Conan: [settings] compiler.version=${_compiler_version}")
    if (_compiler_runtime)
        message(STATUS "CMake-Conan: [settings] compiler.runtime=${_compiler_runtime}")
    endif()
    if (_compiler_runtime_type)
        message(STATUS "CMake-Conan: [settings] compiler.runtime_type=${_compiler_runtime_type}")
    endif()

    set(${compiler} ${_compiler} PARENT_SCOPE)
    set(${compiler_version} ${_compiler_version} PARENT_SCOPE)
    set(${compiler_runtime} ${_compiler_runtime} PARENT_SCOPE)
    set(${compiler_runtime_type} ${_compiler_runtime_type} PARENT_SCOPE)
endfunction()


function(_detect_build_type build_type)
    get_property(multiconfig_generator GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if(NOT multiconfig_generator)
        # Only set when we know we are in a single-configuration generator
        # Note: we may want to fail early if `CMAKE_BUILD_TYPE` is not defined
        set(${build_type} ${CMAKE_BUILD_TYPE} PARENT_SCOPE)
    endif()
endfunction()


macro(_set_conan_compiler_if_appleclang lang command output_variable)
    if(CMAKE_${lang}_COMPILER_ID STREQUAL "AppleClang")
        execute_process(COMMAND xcrun --find ${command}
            OUTPUT_VARIABLE _xcrun_out OUTPUT_STRIP_TRAILING_WHITESPACE)
        cmake_path(GET _xcrun_out PARENT_PATH _xcrun_toolchain_path)
        cmake_path(GET CMAKE_${lang}_COMPILER PARENT_PATH _compiler_parent_path)
        if ("${_xcrun_toolchain_path}" STREQUAL "${_compiler_parent_path}")
            set(${output_variable} "")
        endif()
        unset(_xcrun_out)
        unset(_xcrun_toolchain_path)
        unset(_compiler_parent_path)
    endif()
endmacro()


macro(_append_compiler_executables_configuration)
    set(_conan_c_compiler "")
    set(_conan_cpp_compiler "")
    set(_conan_rc_compiler "")
    set(_conan_compilers_list "")
    if(CMAKE_C_COMPILER)
        set(_conan_c_compiler "\"c\":\"${CMAKE_C_COMPILER}\"")
        _set_conan_compiler_if_appleclang(C cc _conan_c_compiler)
        list(APPEND _conan_compilers_list ${_conan_c_compiler})
    else()
        message(WARNING "CMake-Conan: The C compiler is not defined. "
                        "Please define CMAKE_C_COMPILER or enable the C language.")
    endif()
    if(CMAKE_CXX_COMPILER)
        set(_conan_cpp_compiler "\"cpp\":\"${CMAKE_CXX_COMPILER}\"")
        _set_conan_compiler_if_appleclang(CXX c++ _conan_cpp_compiler)
        list(APPEND _conan_compilers_list ${_conan_cpp_compiler})
    else()
        message(WARNING "CMake-Conan: The C++ compiler is not defined. "
                        "Please define CMAKE_CXX_COMPILER or enable the C++ language.")
    endif()
    if(CMAKE_RC_COMPILER)
        set(_conan_rc_compiler "\"rc\":\"${CMAKE_RC_COMPILER}\"")
        list(APPEND _conan_compilers_list ${_conan_rc_compiler})
        # Not necessary to warn if RC not defined
    endif()
    if(NOT "x${_conan_compilers_list}" STREQUAL "x")
        string(REPLACE ";" "," _conan_compilers_list "${_conan_compilers_list}")
        string(APPEND profile "tools.build:compiler_executables={${_conan_compilers_list}}\n")
    endif()
    unset(_conan_c_compiler)
    unset(_conan_cpp_compiler)
    unset(_conan_rc_compiler)
    unset(_conan_compilers_list)
endmacro()


function(_detect_host_profile output_file)
    _detect_os(os os_api_level os_sdk os_subsystem os_version)
    _detect_arch(arch)
    _detect_compiler(compiler compiler_version compiler_runtime compiler_runtime_type)
    _detect_cxx_standard(compiler_cppstd)
    _detect_lib_cxx(compiler_libcxx)
    _detect_build_type(build_type)

    set(profile "")
    string(APPEND profile "[settings]\n")
    if(arch)
        string(APPEND profile arch=${arch} "\n")
    endif()
    if(os)
        string(APPEND profile os=${os} "\n")
    endif()
    if(os_api_level)
        string(APPEND profile os.api_level=${os_api_level} "\n")
    endif()
    if(os_version)
        string(APPEND profile os.version=${os_version} "\n")
    endif()
    if(os_sdk)
        string(APPEND profile os.sdk=${os_sdk} "\n")
    endif()
    if(os_subsystem)
        string(APPEND profile os.subsystem=${os_subsystem} "\n")
    endif()
    if(compiler)
        string(APPEND profile compiler=${compiler} "\n")
    endif()
    if(compiler_version)
        string(APPEND profile compiler.version=${compiler_version} "\n")
    endif()
    if(compiler_runtime)
        string(APPEND profile compiler.runtime=${compiler_runtime} "\n")
    endif()
    if(compiler_runtime_type)
        string(APPEND profile compiler.runtime_type=${compiler_runtime_type} "\n")
    endif()
    if (NOT compiler_cppstd)
        set(compiler_cppstd 17)
    endif()
    string(APPEND profile compiler.cppstd=${compiler_cppstd} "\n")
    if(compiler_libcxx)
        string(APPEND profile compiler.libcxx=${compiler_libcxx} "\n")
    endif()
    if(build_type)
        string(APPEND profile "build_type=${build_type}\n")
    endif()

    if(NOT DEFINED output_file)
        set(file_name "${CMAKE_BINARY_DIR}/profile")
    else()
        set(file_name ${output_file})
    endif()

    string(APPEND profile "[conf]\n")
    string(APPEND profile "tools.cmake.cmaketoolchain:generator=${CMAKE_GENERATOR}\n")

    # propagate compilers via profile
    _append_compiler_executables_configuration()

    if(os STREQUAL "Android")
        string(APPEND profile "tools.android:ndk_path=${CMAKE_ANDROID_NDK}\n")
    endif()

    message(STATUS "CMake-Conan: Creating profile ${file_name}")
    file(WRITE ${file_name} ${profile})
    message(STATUS "CMake-Conan: Profile: \n${profile}")
endfunction()


function(_conan_profile_detect_default)
    message(STATUS "CMake-Conan: Checking if a default profile exists")
    execute_process(COMMAND ${CONAN_COMMAND} profile path default
                    RESULT_VARIABLE return_code
                    OUTPUT_VARIABLE conan_stdout
                    ERROR_VARIABLE conan_stderr
                    ECHO_ERROR_VARIABLE    # show the text output regardless
                    ECHO_OUTPUT_VARIABLE
                    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    if(NOT ${return_code} EQUAL "0")
        message(STATUS "CMake-Conan: The default profile doesn't exist, detecting it.")
        execute_process(COMMAND ${CONAN_COMMAND} profile detect
            RESULT_VARIABLE return_code
            OUTPUT_VARIABLE conan_stdout
            ERROR_VARIABLE conan_stderr
            ECHO_ERROR_VARIABLE    # show the text output regardless
            ECHO_OUTPUT_VARIABLE
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    endif()
endfunction()


function(_conan_install)
    set(conan_args ${ARGN})
    set(conan_output_folder ${CMAKE_BINARY_DIR}/conan)
    # Invoke "conan install" with the provided arguments
    set(conan_args)
    if (CONAN2_REMOTE)
        message(STATUS "Using Conan2 Remote: ${CONAN2_REMOTE}")
        set(conan_args ${conan_args} -r ${CONAN2_REMOTE})
    endif()

    if(NOT CONAN2_ROOT)
        set(CONAN2_ROOT ${CMAKE_SOURCE_DIR})
    endif()

    message(STATUS "CMake-Conan: conan install ${CONAN2_ROOT} ${conan_args} ${ARGN}")


    # In case there was not a valid cmake executable in the PATH, we inject the
    # same we used to invoke the provider to the PATH
    if(DEFINED PATH_TO_CMAKE_BIN)
        set(old_path $ENV{PATH})
        set(ENV{PATH} "$ENV{PATH}:${PATH_TO_CMAKE_BIN}")
    endif()

    execute_process(
        COMMAND
            ${CONAN_COMMAND} install
                ${CONAN2_ROOT} ${conan_args} -of=${conan_output_folder} ${ARGN} --format=json
        RESULT_VARIABLE return_code
        OUTPUT_VARIABLE conan_stdout
        ERROR_VARIABLE conan_stderr
        ECHO_ERROR_VARIABLE    # show the text output regardless
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

    if(DEFINED PATH_TO_CMAKE_BIN)
        set(ENV{PATH} "${old_path}")
    endif()

    if(NOT "${return_code}" STREQUAL "0")
        execute_process(
            COMMAND ${CONAN_COMMAND} graph explain ${CONAN2_ROOT} ${conan_args} ${ARGN}
            ECHO_ERROR_VARIABLE
            WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
            )
        message(FATAL_ERROR "Conan install failed='${return_code}'")
    endif()

    # the files are generated in a folder that depends on the layout used, if
    # one is specified, but we don't know a priori where this is.
    # TODO: this can be made more robust if Conan can provide this in the json output
    string(JSON conan_generators_folder GET "${conan_stdout}" graph nodes 0 generators_folder)
    cmake_path(CONVERT ${conan_generators_folder} TO_CMAKE_PATH_LIST conan_generators_folder)

    message(STATUS "CMake-Conan: CONAN_GENERATORS_FOLDER=${conan_generators_folder}")
    set_property(GLOBAL PROPERTY CONAN_GENERATORS_FOLDER "${conan_generators_folder}")
    # reconfigure on conanfile changes
    string(JSON conanfile GET "${conan_stdout}" graph nodes 0 label)
    message(STATUS "CMake-Conan: CONANFILE=${CONAN2_ROOT}/${conanfile}")
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CONAN2_ROOT}/${conanfile}")
    # success
    set_property(GLOBAL PROPERTY CONAN_INSTALL_SUCCESS TRUE)

endfunction()

function(_conan_check_remote name url)
    execute_process(COMMAND ${CONAN_COMMAND} remote list
        OUTPUT_VARIABLE remotes
        ECHO_ERROR_VARIABLE
        COMMAND_ERROR_IS_FATAL ANY
    )
    string(REPLACE "\n" ";" remotes "${remotes}")
    set(known_remotes)
    foreach(remote ${remotes})
        string(REGEX MATCH "^([A-Za-z0-9_]+):" remote_name "${remote}")
        list(APPEND known_remotes ${CMAKE_MATCH_1})
    endforeach()
    if (NOT name IN_LIST known_remotes) #check remote missing from list
        message(STATUS "CMake-Conan: adding ${name} to remotes")
        execute_process(COMMAND ${CONAN_COMMAND} remote add ${name} ${url} --force
            ECHO_ERROR_VARIABLE
            COMMAND_ERROR_IS_FATAL ANY
        )
    else()
        message(STATUS "CMake-Conan: ${name} already in remotes")
    endif()
    execute_process(COMMAND ${CONAN_COMMAND} remote auth ${name} --force
        OUTPUT_VARIABLE login
        ECHO_ERROR_VARIABLE
        COMMAND_ERROR_IS_FATAL ANY
    )
    message(STATUS "CMake-Conan: using login info: ${login}")
endfunction()

function(_conan_get_version conan_command conan_current_version)
    execute_process(
        COMMAND ${conan_command} --version
        OUTPUT_VARIABLE conan_output
        RESULT_VARIABLE conan_result
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(conan_result)
        message(FATAL_ERROR "CMake-Conan: Error when trying to run Conan")
    endif()

    string(REGEX MATCH "[0-9]+\\.[0-9]+\\.[0-9]+" conan_version ${conan_output})
    set(${conan_current_version} ${conan_version} PARENT_SCOPE)
endfunction()


function(_conan_version_check)
    set(options )
    set(one_value_args MINIMUM CURRENT)
    set(multi_value_args )
    cmake_parse_arguments(conan_version_check
        "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

    if(NOT conan_version_check_MINIMUM)
        message(FATAL_ERROR "CMake-Conan: Required parameter MINIMUM not set!")
    endif()
        if(NOT conan_version_check_CURRENT)
        message(FATAL_ERROR "CMake-Conan: Required parameter CURRENT not set!")
    endif()

    if(conan_version_check_CURRENT VERSION_LESS conan_version_check_MINIMUM)
        message(FATAL_ERROR "CMake-Conan: Conan version must be ${conan_version_check_MINIMUM} or later")
    endif()
endfunction()


macro(_construct_profile_argument argument_variable profile_list)
    set(${argument_variable} "")
    if("${profile_list}" STREQUAL "CONAN_HOST_PROFILE")
        set(_arg_flag "--profile:host=")
    elseif("${profile_list}" STREQUAL "CONAN_BUILD_PROFILE")
        set(_arg_flag "--profile:build=")
    endif()

    set(_profile_list "${${profile_list}}")
    list(TRANSFORM _profile_list REPLACE "auto-cmake" "${CMAKE_BINARY_DIR}/conan_host_profile")
    list(TRANSFORM _profile_list PREPEND ${_arg_flag})
    set(${argument_variable} ${_profile_list})

    unset(_arg_flag)
    unset(_profile_list)
endmacro()

macro(_conan2_install)
    set_property(GLOBAL PROPERTY CONAN_PROVIDE_DEPENDENCY_INVOKED TRUE)
    get_property(_conan_install_success GLOBAL PROPERTY CONAN_INSTALL_SUCCESS)
    if(NOT _conan_install_success)
        find_program(CONAN_COMMAND "conan")
        _conan_get_version(${CONAN_COMMAND} CONAN_CURRENT_VERSION)
        _conan_version_check(MINIMUM ${CONAN_MINIMUM_VERSION} CURRENT ${CONAN_CURRENT_VERSION})
        message(STATUS "CMake-Conan: Installing dependencies with Conan")
        if("default" IN_LIST CONAN_HOST_PROFILE OR "default" IN_LIST CONAN_BUILD_PROFILE)
            _conan_profile_detect_default()
        endif()
        if("auto-cmake" IN_LIST CONAN_HOST_PROFILE)
            _detect_host_profile(${CMAKE_BINARY_DIR}/conan_host_profile)
        endif()
        _construct_profile_argument(_host_profile_flags CONAN_HOST_PROFILE)
        _construct_profile_argument(_build_profile_flags CONAN_BUILD_PROFILE)
        if (CONAN2_REMOTE)
            _conan_check_remote(${CONAN2_REMOTE} ${CONAN2_REMOTE_URL})
        endif()
        get_property(_multiconfig_generator GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
        if(NOT _multiconfig_generator)
            message(STATUS "CMake-Conan: Installing single configuration ${CMAKE_BUILD_TYPE}")
            _conan_install(${_host_profile_flags} ${_build_profile_flags} ${CONAN_INSTALL_ARGS} ${generator})
        else()
            message(STATUS "CMake-Conan: Installing both Debug and Release")
            _conan_install(${_host_profile_flags} ${_build_profile_flags} -s build_type=Release ${CONAN_INSTALL_ARGS} ${generator})
            _conan_install(${_host_profile_flags} ${_build_profile_flags} -s build_type=Debug ${CONAN_INSTALL_ARGS} ${generator})
        endif()
        unset(_host_profile_flags)
        unset(_build_profile_flags)
    endif()
    unset(_conan_install_success)

    get_property(_conan_generators_folder GLOBAL PROPERTY CONAN_GENERATORS_FOLDER)
    include(${_conan_generators_folder}/conan_toolchain.cmake)
    list(PREPEND CMAKE_PREFIX_PATH ${_conan_generators_folder})
    unset(_conan_generators_folder)
endmacro()

# Configurable variables for Conan profiles
set(CONAN_HOST_PROFILE "default;auto-cmake" CACHE STRING "Conan host profile")
set(CONAN_BUILD_PROFILE "default" CACHE STRING "Conan build profile")
set(CONAN_INSTALL_ARGS "--build=missing" CACHE STRING "Command line arguments for conan install")

find_program(_cmake_program NAMES cmake NO_PACKAGE_ROOT_PATH NO_CMAKE_PATH NO_CMAKE_ENVIRONMENT_PATH NO_CMAKE_SYSTEM_PATH NO_CMAKE_FIND_ROOT_PATH)
if(NOT _cmake_program)
    get_filename_component(PATH_TO_CMAKE_BIN "${CMAKE_COMMAND}" DIRECTORY)
    set(PATH_TO_CMAKE_BIN "${PATH_TO_CMAKE_BIN}" CACHE INTERNAL "Path where the CMake executable is")
endif()

cmake_policy(POP)

_conan2_install()