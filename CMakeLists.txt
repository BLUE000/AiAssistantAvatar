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
    src/ui/avatar_skin_builder_dialog.cpp
    src/ui/history_viewer_dialog.cpp
    src/utils/json_comment_remover.cpp
    src/utils/wakeword_matcher.cpp
    src/utils/process_utils.cpp
    src/stt/stt_text_normalizer.cpp
    src/tts/bouyomichan_client.cpp

    src/ui/history_viewer_dialog.h
    src/ui/rate_limit_tab_widget.cpp
    src/ui/rate_limit_tab_widget.h
    src/twitch/twitch_reader.cpp
    src/stt/stt_manager.cpp
    src/stt/istt_engine.cpp
    src/stt/whisper_engine.cpp
    src/stt/sapi_engine.cpp
    src/ai/ai_client_manager.cpp
    src/ai/twitch_helix_client.cpp
    src/ai/iai_client.cpp
    src/ai/mistral_ai_client.cpp
    src/ai/groq_ai_client.cpp
    src/ai/gemini_ai_client.cpp
    src/ai/huggingface_ai_client.cpp
    src/ai/openrouter_ai_client.cpp
    src/ai/sakura_ai_client.cpp
    src/ai/dummy_ai_client.cpp
    src/ai/rate_limit_tracker.cpp
    src/ai/system_response_manager.cpp
    src/ai/ai_random_utils.cpp
    src/ai/ai_router.cpp
    src/ai/manager_context_evaluator.cpp
    src/search/isearch_provider.cpp
    src/search/search_manager.cpp
    src/search/tavily_search_provider.cpp
    src/search/duckduckgo_search_provider.cpp
    src/search/markdown_table_engine.cpp
    src/discord/discord_reader.cpp
    src/obs/obs_http_server.cpp
    src/moderation/score_moderation_engine.cpp
    src/observer/community_observer_engine.cpp
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

add_custom_command(TARGET AiAssistantAvatar POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/Lib/TransCipher/bin/libTransCipher.dll"
            "$<TARGET_FILE_DIR:AiAssistantAvatar>/libTransCipher.dll"
    COMMENT "Copying libTransCipher.dll to target directory"
)

# 独立EXEツール: AvatarSkinBuilder
set(SKIN_BUILDER_SOURCES
    src/skin_builder_main.cpp
    src/ui/avatar_skin_builder_dialog.cpp
)
add_executable(AvatarSkinBuilder WIN32 ${SKIN_BUILDER_SOURCES})
target_include_directories(AvatarSkinBuilder PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(AvatarSkinBuilder PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)

# 独立CLIツール: CommunityObserver
set(OBSERVER_SOURCES
    src/observer/main.cpp
    src/observer/community_observer_engine.cpp
)
add_executable(CommunityObserver ${OBSERVER_SOURCES})
target_include_directories(CommunityObserver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(CommunityObserver PRIVATE Qt6::Core)

# 独立CLIツール: WebSearcher
set(WEB_SEARCHER_SOURCES
    src/search/web_searcher_main.cpp
    src/search/isearch_provider.cpp
    src/search/tavily_search_provider.cpp
    src/search/duckduckgo_search_provider.cpp
)
add_executable(WebSearcher ${WEB_SEARCHER_SOURCES})
target_include_directories(WebSearcher PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(WebSearcher PRIVATE Qt6::Core Qt6::Network)

# 独立CLIツール: TwitchIntroGenerator
set(INTRO_GENERATOR_SOURCES
    src/twitch/twitch_intro_generator_main.cpp
    src/ai/twitch_helix_client.cpp
    src/ai/ai_client_manager.cpp
    src/ai/iai_client.cpp
    src/ai/mistral_ai_client.cpp
    src/ai/groq_ai_client.cpp
    src/ai/gemini_ai_client.cpp
    src/ai/huggingface_ai_client.cpp
    src/ai/openrouter_ai_client.cpp
    src/ai/sakura_ai_client.cpp
    src/ai/dummy_ai_client.cpp
    src/ai/rate_limit_tracker.cpp
    src/ai/system_response_manager.cpp
    src/ai/ai_random_utils.cpp
    src/ai/ai_router.cpp
    src/ai/manager_context_evaluator.cpp
    src/search/isearch_provider.cpp
    src/search/search_manager.cpp
    src/search/tavily_search_provider.cpp
    src/search/duckduckgo_search_provider.cpp
    src/search/markdown_table_engine.cpp
    src/moderation/score_moderation_engine.cpp
    src/observer/community_observer_engine.cpp
    src/utils/json_comment_remover.cpp
    src/utils/wakeword_matcher.cpp
    src/utils/process_utils.cpp
    src/stt/stt_text_normalizer.cpp
)
add_executable(TwitchIntroGenerator ${INTRO_GENERATOR_SOURCES})
target_include_directories(TwitchIntroGenerator PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_definitions(TwitchIntroGenerator PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(TwitchIntroGenerator PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Network
    TransCipher
)

# 独立CLIツール: GeminiChatter
set(GEMINI_CHATTER_SOURCES
    src/ai/gemini_chatter_main.cpp
    src/ai/gemini_ai_client.cpp
    src/ai/iai_client.cpp
    src/search/isearch_provider.cpp
    src/search/search_manager.cpp
    src/search/tavily_search_provider.cpp
    src/search/duckduckgo_search_provider.cpp
    src/search/markdown_table_engine.cpp
    src/utils/json_comment_remover.cpp
    src/utils/process_utils.cpp
)
add_executable(GeminiChatter ${GEMINI_CHATTER_SOURCES})
target_include_directories(GeminiChatter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_definitions(GeminiChatter PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(GeminiChatter PRIVATE
    Qt6::Core
    Qt6::Network
)

# 独立CLIツール: MistralChatter (F-44)
set(MISTRAL_CHATTER_SOURCES
    src/ai/mistral_chatter_main.cpp
    src/ai/mistral_ai_client.cpp
    src/ai/iai_client.cpp
    src/search/isearch_provider.cpp
    src/search/search_manager.cpp
    src/search/tavily_search_provider.cpp
    src/search/duckduckgo_search_provider.cpp
    src/search/markdown_table_engine.cpp
    src/utils/json_comment_remover.cpp
    src/utils/process_utils.cpp
)
add_executable(MistralChatter ${MISTRAL_CHATTER_SOURCES})
target_include_directories(MistralChatter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_definitions(MistralChatter PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(MistralChatter PRIVATE
    Qt6::Core
    Qt6::Network
)

# 独立CLIツール: GroqChatter (F-46)
set(GROQ_CHATTER_SOURCES
    src/ai/groq_chatter_main.cpp
    src/ai/groq_ai_client.cpp
    src/ai/iai_client.cpp
    src/search/isearch_provider.cpp
    src/search/search_manager.cpp
    src/search/tavily_search_provider.cpp
    src/search/duckduckgo_search_provider.cpp
    src/search/markdown_table_engine.cpp
    src/utils/json_comment_remover.cpp
    src/utils/process_utils.cpp
)
add_executable(GroqChatter ${GROQ_CHATTER_SOURCES})
target_include_directories(GroqChatter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_definitions(GroqChatter PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(GroqChatter PRIVATE
    Qt6::Core
    Qt6::Network
)

# 独立CLIツール: SakuraChatter (F-46)
set(SAKURA_CHATTER_SOURCES
    src/ai/sakura_chatter_main.cpp
    src/ai/sakura_ai_client.cpp
    src/ai/iai_client.cpp
    src/search/isearch_provider.cpp
    src/search/search_manager.cpp
    src/search/tavily_search_provider.cpp
    src/search/duckduckgo_search_provider.cpp
    src/search/markdown_table_engine.cpp
    src/utils/json_comment_remover.cpp
    src/utils/process_utils.cpp
)
add_executable(SakuraChatter ${SAKURA_CHATTER_SOURCES})
target_include_directories(SakuraChatter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_definitions(SakuraChatter PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(SakuraChatter PRIVATE
    Qt6::Core
    Qt6::Network
)

# 独立CLIツール: HuggingFaceChatter (F-51)
set(HUGGINGFACE_CHATTER_SOURCES
    src/ai/huggingface_chatter_main.cpp
    src/ai/huggingface_ai_client.cpp
    src/ai/iai_client.cpp
    src/search/isearch_provider.cpp
    src/search/search_manager.cpp
    src/search/tavily_search_provider.cpp
    src/search/duckduckgo_search_provider.cpp
    src/search/markdown_table_engine.cpp
    src/utils/json_comment_remover.cpp
    src/utils/process_utils.cpp
)
add_executable(HuggingFaceChatter ${HUGGINGFACE_CHATTER_SOURCES})
target_include_directories(HuggingFaceChatter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_definitions(HuggingFaceChatter PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(HuggingFaceChatter PRIVATE
    Qt6::Core
    Qt6::Network
)

# 独立CLIツール: OpenRouterChatter (F-51)
set(OPENROUTER_CHATTER_SOURCES
    src/ai/openrouter_chatter_main.cpp
    src/ai/openrouter_ai_client.cpp
    src/ai/iai_client.cpp
    src/search/isearch_provider.cpp
    src/search/search_manager.cpp
    src/search/tavily_search_provider.cpp
    src/search/duckduckgo_search_provider.cpp
    src/search/markdown_table_engine.cpp
    src/utils/json_comment_remover.cpp
    src/utils/process_utils.cpp
)
add_executable(OpenRouterChatter ${OPENROUTER_CHATTER_SOURCES})
target_include_directories(OpenRouterChatter PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
)
target_compile_definitions(OpenRouterChatter PRIVATE
    PROJECT_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(OpenRouterChatter PRIVATE
    Qt6::Core
    Qt6::Network
)

# テストビルドの有効化
enable_testing()
add_subdirectory(test)
