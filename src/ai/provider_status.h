#pragma once
#include <QString>
#include <QDateTime>

/// 各AIプロバイダの状態・制限・仕様を保持する構造体
struct ProviderStatus {
    QString   provider;           // "groq" / "cerebras" / "mistral" / "dummy"
    bool      available = true;   // レートリミット未到達なら true

    // --- リクエスト制限 ---
    int rpmMax       = -1;         // 最大RPM（手動設定 or APIヘッダーから取得）
    int rpmRemaining = -1;         // 残りRPM（APIレスポンスヘッダーから更新）
    int rpdMax       = -1;         // 最大RPD
    int rpdRemaining = -1;         // 残りRPD

    // --- トークン制限 ---
    int tpmMax       = -1;         // 最大TPM
    int tpmRemaining = -1;         // 残りTPM
    int tpdMax       = -1;         // 最大TPD
    int tpdRemaining = -1;         // 残りTPD

    // --- モデル仕様（自動取得 or 手動設定） ---
    int    contextWindow = 0;     // コンテキストウィンドウ (tokens)
    bool   toolCall      = false; // Function Calling サポート
    bool   supportsDiff  = false; // Diff出力サポート（将来拡張用）
    double cost          = 0.0;   // コスト (0.0 = 無料)
    int    latencyMs     = 0;     // 実測移動平均レイテンシ (ms)

    QDateTime nextResetAt;        // 最短リセット時刻（全枯渇時のメッセージ生成に使用）
};
