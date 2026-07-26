#include "history_viewer_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QDateTime>
#include <QDebug>

HistoryViewerDialog::HistoryViewerDialog(AIClientManager *aiManager, QWidget *parent)
    : QDialog(parent)
    , m_aiManager(aiManager)
{
    setWindowTitle("会話履歴ビューア (ページネーション対応)");
    resize(720, 520);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // 上部ステータスバー
    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("font-weight: bold; color: #2c3e50; padding: 4px;");
    mainLayout->addWidget(m_statusLabel);

    // 上部コントロールバー（表示件数・ページ選択）
    QHBoxLayout *topControlLayout = new QHBoxLayout();
    topControlLayout->addWidget(new QLabel("1ページあたりの表示件数:", this));

    m_pageSizeCombo = new QComboBox(this);
    m_pageSizeCombo->addItem("10件", 10);
    m_pageSizeCombo->addItem("50件", 50);
    m_pageSizeCombo->addItem("100件", 100);
    m_pageSizeCombo->setCurrentIndex(1); // 50件デフォルト
    connect(m_pageSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &HistoryViewerDialog::onPageSizeChanged);
    topControlLayout->addWidget(m_pageSizeCombo);

    topControlLayout->addStretch();

    m_prevButton = new QPushButton("◀ 前へ", this);
    connect(m_prevButton, &QPushButton::clicked, this, &HistoryViewerDialog::onPrevPage);
    topControlLayout->addWidget(m_prevButton);

    m_pageLabel = new QLabel("1 / 1 ページ", this);
    m_pageLabel->setStyleSheet("margin: 0 8px; font-size: 13px;");
    topControlLayout->addWidget(m_pageLabel);

    m_nextButton = new QPushButton("次へ ▶", this);
    connect(m_nextButton, &QPushButton::clicked, this, &HistoryViewerDialog::onNextPage);
    topControlLayout->addWidget(m_nextButton);

    mainLayout->addLayout(topControlLayout);

    // 会話ログ表示テキストエリア (ReadOnly)
    m_textEdit = new QTextEdit(this);
    m_textEdit->setReadOnly(true);
    m_textEdit->setStyleSheet("font-family: Consolas, 'Yu Gothic', monospace; font-size: 13px; background-color: #f8f9fa; border: 1px solid #ced4da; padding: 8px;");
    mainLayout->addWidget(m_textEdit);

    // 下部ボタンアクションバー
    QHBoxLayout *actionLayout = new QHBoxLayout();

    m_exportButton = new QPushButton("📄 平文エクスポート (.txt)", this);
    m_exportButton->setStyleSheet("padding: 6px 12px; font-weight: bold;");
    connect(m_exportButton, &QPushButton::clicked, this, &HistoryViewerDialog::onExportText);
    actionLayout->addWidget(m_exportButton);

    m_summarizeButton = new QPushButton("⚡ 今すぐサマリ化", this);
    m_summarizeButton->setStyleSheet("padding: 6px 12px; font-weight: bold; background-color: #e74c3c; color: white;");
    connect(m_summarizeButton, &QPushButton::clicked, this, &HistoryViewerDialog::onForceSummarize);
    actionLayout->addWidget(m_summarizeButton);

    actionLayout->addStretch();

    QPushButton *closeBtn = new QPushButton("閉じる", this);
    closeBtn->setStyleSheet("padding: 6px 16px;");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    actionLayout->addWidget(closeBtn);

    mainLayout->addLayout(actionLayout);

    updateView();
}

void HistoryViewerDialog::onPageSizeChanged(int index) {
    Q_UNUSED(index);
    m_pageSize = m_pageSizeCombo->currentData().toInt();
    m_currentPage = 1;
    updateView();
}

void HistoryViewerDialog::onPrevPage() {
    if (m_currentPage > 1) {
        m_currentPage--;
        updateView();
    }
}

void HistoryViewerDialog::onNextPage() {
    int totalPages = qMax(1, (m_allEntries.size() + m_pageSize - 1) / m_pageSize);
    if (m_currentPage < totalPages) {
        m_currentPage++;
        updateView();
    }
}

void HistoryViewerDialog::updateView() {
    if (m_aiManager) {
        m_allEntries = m_aiManager->getConversationEntries();
    }

    int totalCount = m_allEntries.size();
    int unsummarizedCount = 0;
    int summarizedCount = 0;

    for (const auto &entry : m_allEntries) {
        if (entry.isSummarized) {
            summarizedCount++;
        } else {
            unsummarizedCount++;
        }
    }

    m_statusLabel->setText(QString("【会話ログ全 %1 件】 未サマリ生ログ: %2 件 / 要約済み: %3 件")
                               .arg(totalCount).arg(unsummarizedCount).arg(summarizedCount));

    int totalPages = qMax(1, (totalCount + m_pageSize - 1) / m_pageSize);
    if (m_currentPage > totalPages) {
        m_currentPage = totalPages;
    }

    m_pageLabel->setText(QString("%1 / %2 ページ").arg(m_currentPage).arg(totalPages));
    m_prevButton->setEnabled(m_currentPage > 1);
    m_nextButton->setEnabled(m_currentPage < totalPages);

    // 指定ページのログのみをレンダリング (HTML 描画)
    int startIndex = (m_currentPage - 1) * m_pageSize;
    int endIndex = qMin(totalCount, startIndex + m_pageSize);

    QString html;
    html += "<div style='line-height: 1.5;'>";

    if (totalCount == 0) {
        html += "<p style='color: #7f8c8d; text-align: center;'>会話履歴はありません。</p>";
    } else {
        for (int i = startIndex; i < endIndex; ++i) {
            const auto &entry = m_allEntries.at(i);
            QString tagHtml;
            if (entry.isSummarized) {
                tagHtml = "<span style='background-color: #3498db; color: white; padding: 2px 6px; border-radius: 3px; font-size: 11px; font-weight: bold;'>[サマリ化済]</span>";
            } else {
                tagHtml = "<span style='background-color: #2ecc71; color: white; padding: 2px 6px; border-radius: 3px; font-size: 11px; font-weight: bold;'>[未サマリ]</span>";
            }

            QString senderColor = (entry.sender == "ユーザー") ? "#2980b9" : "#8e44ad";
            if (entry.sender == "システム要約") senderColor = "#d35400";

            html += QString("<div style='margin-bottom: 10px; padding: 6px; background-color: white; border-left: 4px solid %1; border-radius: 4px;'>")
                        .arg(senderColor);
            html += QString("<div>%1 <strong style='color: %2;'>%3</strong> <span style='color: #95a5a6; font-size: 11px;'>[%4]</span></div>")
                        .arg(tagHtml).arg(senderColor).arg(entry.sender.toHtmlEscaped()).arg(entry.timestamp);
            html += QString("<div style='margin-top: 4px; color: #2c3e50; white-space: pre-wrap;'>%1</div>")
                        .arg(entry.text.toHtmlEscaped());
            html += "</div>";
        }
    }

    html += "</div>";
    m_textEdit->setHtml(html);
}

void HistoryViewerDialog::onExportText() {
    if (m_allEntries.isEmpty()) {
        QMessageBox::information(this, "通知", "保存する会話履歴がありません。");
        return;
    }

    QString defaultName = QString("ChatHistory_%1.txt").arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    QString filePath = QFileDialog::getSaveFileName(this, "会話履歴の保存 (平文エクスポート)", defaultName, "テキストファイル (*.txt)");
    if (filePath.isEmpty()) return;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "エラー", "ファイルの書き込みに失敗しました。");
        return;
    }

    QTextStream out(&file);
    out << "========================================" << "\n";
    out << "  AiAssistantAvatar 会話履歴エクスポート" << "\n";
    out << "  出力日時: " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "\n";
    out << "  全ログ件数: " << m_allEntries.size() << " 件" << "\n";
    out << "========================================" << "\n\n";

    for (const auto &entry : m_allEntries) {
        QString statusStr = entry.isSummarized ? "[サマリ化済]" : "[未サマリ]";
        out << statusStr << " " << entry.sender << " [" << entry.timestamp << "]\n";
        out << entry.text << "\n";
        out << "----------------------------------------\n";
    }

    file.close();
    QMessageBox::information(this, "成功", QString("会話履歴をテキスト保存しました:\n%1").arg(filePath));
}

void HistoryViewerDialog::onForceSummarize() {
    if (!m_aiManager) return;

    QMessageBox::StandardButton res = QMessageBox::question(
        this, "強制サマリ化の確認",
        "現在の未サマリ会話ログを今すぐ要約（トークン要約圧縮）し、記憶に統合しますか？",
        QMessageBox::Yes | QMessageBox::No);

    if (res == QMessageBox::Yes) {
        m_aiManager->forceSummarizeHistory();
        QMessageBox::information(this, "処理完了", "会話ログのサマリ化（要約圧縮）要求を送信しました。");
        updateView();
    }
}
