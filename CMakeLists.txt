cmake_minimum_required(VERSION 3.20)
project(OriWotWVR C CXX ASM_MASM)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
set(CMAKE_CXX_STANDARD 20)
set(MINHOOK_DIR "" CACHE PATH "MinHook source folder")
set(OUT "${CMAKE_SOURCE_DIR}/../build")
set(FONTS_DIR "" CACHE PATH "Folder with MarcellusSC-Regular.ttf and AlegreyaSans-Regular.ttf (Google Fonts, OFL)")

# winhttp.dll proxy
add_library(winhttp SHARED proxy/winhttp_proxy.cpp proxy/thunks.asm proxy/winhttp.def)
set_target_properties(winhttp PROPERTIES OUTPUT_NAME winhttp RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUT}")

# native mod
add_library(minhook STATIC ${MINHOOK_DIR}/src/buffer.c ${MINHOOK_DIR}/src/hook.c ${MINHOOK_DIR}/src/trampoline.c ${MINHOOK_DIR}/src/hde/hde64.c)
target_include_directories(minhook PUBLIC ${MINHOOK_DIR}/include)
set(OPENXR_DIR "" CACHE PATH "Khronos OpenXR loader folder")
set(IMGUI_DIR "" CACHE PATH "Dear ImGui 1.93 folder")
add_library(OriWotWVRNative SHARED native/OriWotWVRNative.cpp xr/WotwXR.cpp xr/MenuDraw.cpp
    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)
target_include_directories(OriWotWVRNative PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends)
target_include_directories(OriWotWVRNative PRIVATE ${OPENXR_DIR}/include)
target_link_libraries(OriWotWVRNative PRIVATE minhook d3d11 dxgi winmm)
add_custom_command(TARGET OriWotWVRNative POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${OPENXR_DIR}/x64/bin/openxr_loader.dll" "${OUT}/openxr_loader.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${FONTS_DIR}/MarcellusSC-Regular.ttf" "${FONTS_DIR}/AlegreyaSans-Regular.ttf" "${OUT}/")
set_target_properties(OriWotWVRNative PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE "${OUT}")
