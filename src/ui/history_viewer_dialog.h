#pragma once
#include <QDialog>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QList>
#include <QThread>
#include "../ai/ai_client_manager.h"

class HistoryViewerDialog : public QDialog {
    Q_OBJECT
public:
    explicit HistoryViewerDialog(AIClientManager *aiManager, QWidget *parent = nullptr);
    ~HistoryViewerDialog() override;

private slots:
    void onPageSizeChanged(int index);
    void onPrevPage();
    void onNextPage();
    void onExportText();
    void onForceSummarize();
    void onEntriesLoaded(const QList<ConversationEntry> &entries);

private:
    void startAsyncLoad();
    void renderCurrentPage();

    AIClientManager *m_aiManager;
    QList<ConversationEntry> m_allEntries;
    bool m_isLoading = false;
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
    QThread *m_loadThread = nullptr;
};
