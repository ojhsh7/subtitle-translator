#pragma once
#include <QWidget>
class HistoryStore;
class QLabel;
class QLineEdit;
class QTableWidget;
class QComboBox;

// Recent translations: search, original and translation side by side, copy, TXT/SRT export.
class HistoryWindow final : public QWidget {
public:
    explicit HistoryWindow(HistoryStore* store, QWidget* parent = nullptr);
    void refresh();
    QString filterText() const;
private:
    friend class WindowChecks;
    void copySelection();
    void exportTo(bool srt);
    QList<int> visibleEntries() const;
    HistoryStore* store_;
    QLineEdit* search_;
    QComboBox* mode_;
    QTableWidget* table_;
    QLabel* note_;
};
