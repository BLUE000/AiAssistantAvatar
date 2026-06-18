# Lib/CMakeLists.txt
cmake_minimum_required(VERSION 3.20)

# TrustChain をサブプロジェクトとして登録
add_subdirectory(TrustChain)

# TransCipher (ビルド済みのインポートライブラリ) をターゲットとして登録
add_library(TransCipher SHARED IMPORTED GLOBAL)

set_target_properties(TransCipher PROPERTIES
    IMPORTED_IMPLIB "${CMAKE_CURRENT_SOURCE_DIR}/TransCipher/lib/libTransCipher.dll.a"
    IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/TransCipher/bin/libTransCipher.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/TransCipher/include"
)
