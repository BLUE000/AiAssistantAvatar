cmake_minimum_required(VERSION 3.16)
project(AiAssistantAvatarTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Qt6 パッケージのロード
find_package(Qt6 COMPONENTS Core Gui Network WebSockets Test REQUIRED)

# Google Test の取得
include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# テストに必要なアプリケーションのソースファイルを直接ビルド
set(TEST_SOURCES
    test_main.cpp
    test_twitch_reader.cpp
    ../src/twitch/twitch_reader.cpp
)

add_executable(AiAssistantAvatarTest ${TEST_SOURCES})

# インクルードパス
target_include_directories(AiAssistantAvatarTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# リンク先ライブラリ
target_link_libraries(AiAssistantAvatarTest PRIVATE
    gtest
    gtest_main
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    Qt6::WebSockets
    Qt6::Test
)

add_test(NAME TwitchReaderTest COMMAND AiAssistantAvatarTest)
