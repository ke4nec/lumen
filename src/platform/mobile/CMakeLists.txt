# v0.3 阶段8E: 移动端 host 接缝库。
#
# 只依赖 lumen-core（HostEvent/WindowMetrics/AppLifecycle 值类型）；不含
# SDL 或平台 SDK 类型，Android NDK / iOS Xcode 交叉编译目标直接链接本
# 库 + lumen-core/layout/render(CPU)/text/accessibility/widgets 组成
# native host（plan §2.1/§4 8E）。native 胶水（JNI/NativeActivity、
# Objective-C++）是各平台仓库外的可选目标，不进入本构建。

add_library(lumen-mobile-host STATIC
    mobile_host_seam.cpp
)

target_include_directories(lumen-mobile-host PUBLIC
    ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(lumen-mobile-host PUBLIC lumen-core)
target_compile_features(lumen-mobile-host PUBLIC cxx_std_20)
