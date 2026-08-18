cmake_minimum_required(VERSION 3.16)
project(AiAssistantAvatarTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Qt6 パッケージのロード
find_package(Qt6 COMPONENTS Core Gui Widgets Network WebSockets Test REQUIRED)

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
    test_ai_client.cpp
    test_search_manager.cpp
    ../src/twitch/twitch_reader.cpp
    ../src/discord/discord_reader.cpp
    ../src/core_module.cpp
    ../src/ai/ai_client_manager.cpp
    ../src/utils/json_comment_remover.cpp
    ../src/utils/wakeword_matcher.cpp
    ../src/stt/stt_text_normalizer.cpp
    ../src/tts/bouyomichan_client.cpp


    ../src/ai/twitch_helix_client.cpp
    ../src/ai/iai_client.cpp
    ../src/ai/mistral_ai_client.cpp
    ../src/ai/groq_ai_client.cpp
    ../src/ai/huggingface_ai_client.cpp
    ../src/ai/openrouter_ai_client.cpp
    ../src/ai/sakura_ai_client.cpp
    ../src/ai/dummy_ai_client.cpp
    ../src/ai/rate_limit_tracker.cpp
    ../src/ai/system_response_manager.cpp
    ../src/ai/ai_random_utils.cpp
    ../src/ai/ai_router.cpp
    ../src/search/isearch_provider.cpp
    ../src/search/search_manager.cpp
    ../src/search/tavily_search_provider.cpp
    ../src/search/duckduckgo_search_provider.cpp
    ../src/search/markdown_table_engine.cpp
    ../src/ui/avatar_window.cpp
    ../src/ui/avatar_skin_builder_dialog.cpp
    ../src/ui/history_viewer_dialog.cpp
    ../src/ui/rate_limit_tab_widget.cpp
    test_obs_http_server.cpp
    ../src/obs/obs_http_server.cpp
    test_score_moderation.cpp
    ../src/moderation/score_moderation_engine.cpp
    test_community_observer.cpp
    ../src/observer/community_observer_engine.cpp
)

add_executable(AiAssistantAvatarTest ${TEST_SOURCES})

# インクルードパス
target_include_directories(AiAssistantAvatarTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CMAKE_BINARY_DIR}
)

target_compile_definitions(AiAssistantAvatarTest PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/.."
)

# リンク先ライブラリ
target_link_libraries(AiAssistantAvatarTest PRIVATE
    gtest
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::WebSockets
    Qt6::Test
    TransCipher
)

# テスト実行環境（実行ファイルと同階層の Config フォルダ）の自動生成およびサンプル設定コピー
add_custom_command(TARGET AiAssistantAvatarTest POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:AiAssistantAvatarTest>/Config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/../Config/local_settings.json.sample"
            "$<TARGET_FILE_DIR:AiAssistantAvatarTest>/Config/local_settings.json.sample"
    COMMENT "Copying test configuration files to target Config/ directory"
)

add_test(NAME AiAssistantAvatarTest COMMAND AiAssistantAvatarTest WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
