#pragma once
#include <QDialog>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QList>
#include "../ai/ai_client_manager.h"

class HistoryViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit HistoryViewerDialog(AIClientManager *aiManager, QWidget *parent = nullptr);
    ~HistoryViewerDialog() override = default;

private slots:
    void onPageSizeChanged(int index);
    void onPrevPage();
    void onNextPage();
    void onExportText();
    void onForceSummarize();

private:
    void updateView();

    AIClientManager *m_aiManager;
    QList<ConversationEntry> m_allEntries;
    int m_pageSize = 50;
    int m_currentPage = 1;

    QLabel *m_statusLabel;
    QTextEdit *m_textEdit;
    QComboBox *m_pageSizeCombo;
    QPushButton *m_prevButton;
    QPushButton *m_nextButton;
    QLabel *m_pageLabel;
    QPushButton *m_exportButton;
    QPushButton *m_summarizeButton;
};
