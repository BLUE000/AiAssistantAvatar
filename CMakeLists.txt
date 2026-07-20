cmake_minimum_required(VERSION 3.20)

# ローカル環境設定（コンパイラ・Qtパスなど）のインポート
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/local_env.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/local_env.cmake")
endif()

# 1. プロジェクト名とバージョン定義
project(AiAssistantAvatar VERSION 0.9.0 LANGUAGES CXX)

# バージョン情報をヘッダーファイルとして自動生成し、プログラムから参照できるようにする
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/version.h"
)

# C++規格の設定
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6の検出
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network WebSockets)
qt_standard_project_setup()

# 依存サブフォルダ/ライブラリの登録
# trustchain_credentials.cmake のインポート
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/trustchain_credentials.cmake")
    include("${CMAKE_CURRENT_SOURCE_DIR}/trustchain_credentials.cmake")
endif()

# Lib ディレクトリの読み込み (TrustChain, TransCipher)
add_subdirectory(Lib)

# ターゲット（実行ファイル）の定義
set(SOURCES
    src/main.cpp
    src/core_module.cpp
    src/ui/avatar_window.cpp
    src/twitch/twitch_reader.cpp
    src/stt/stt_manager.cpp
    src/stt/istt_engine.cpp
    src/stt/whisper_engine.cpp
    src/stt/sapi_engine.cpp
    src/ai/ai_client_manager.cpp
    src/ai/twitch_helix_client.cpp
    src/ai/iai_client.cpp
    src/ai/mistral_ai_client.cpp
    src/ai/cerebras_ai_client.cpp
    src/ai/groq_ai_client.cpp
    src/ai/dummy_ai_client.cpp
    src/ai/rate_limit_tracker.cpp
    src/ai/system_response_manager.cpp
    src/ai/ai_router.cpp
    src/search/isearch_provider.cpp
    src/search/search_manager.cpp
    src/search/tavily_search_provider.cpp
    src/search/duckduckgo_search_provider.cpp
    src/discord/discord_reader.cpp
    src/obs/obs_http_server.cpp
)

add_executable(AiAssistantAvatar WIN32 ${SOURCES})

if(WIN32)
    set_target_properties(AiAssistantAvatar PROPERTIES WIN32_EXECUTABLE TRUE)
    if(MINGW)
        target_link_options(AiAssistantAvatar PRIVATE "-mwindows" "-Wl,--subsystem,windows")
    endif()
endif()


# インクルードパスの設定（生成された version.h がある binary_dir も追加）
target_include_directories(AiAssistantAvatar PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
)

# 開発用の絶対プロジェクトパスをコンパイル時マクロとして定義
target_compile_definitions(AiAssistantAvatar PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Qtライブラリおよび独自ライブラリのリンク
target_link_libraries(AiAssistantAvatar PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    TransCipher
)

# TrustChain セットアップ (魔法の3行の適用)
trustchain_setup_target(AiAssistantAvatar)
target_link_libraries(AiAssistantAvatar PRIVATE trustchain)

# テストビルドの有効化
enable_testing()
add_subdirectory(test)
