#include "history_window.h"
#include "history_store.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSettings>
#include <QShortcut>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
QString modeName(const QString& id) {
    if (id == QLatin1String("game")) return QStringLiteral("게임");
    if (id == QLatin1String("video")) return QStringLiteral("영상");
    if (id == QLatin1String("text")) return QStringLiteral("글");
    if (id == QLatin1String("file")) return QStringLiteral("자막 파일");
    return id;
}
}

HistoryWindow::HistoryWindow(HistoryStore* store, QWidget* parent) : QWidget(parent, Qt::Window), store_(store) {
    setWindowTitle(QStringLiteral("번역 기록"));
    resize(900, 560);
    auto* layout = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    search_ = new QLineEdit;
    search_->setPlaceholderText(QStringLiteral("원문이나 번역에서 찾기"));
    search_->setClearButtonEnabled(true);
    search_->setAccessibleName(QStringLiteral("기록 검색"));
    mode_ = new QComboBox;
    mode_->addItem(QStringLiteral("모든 모드"), QString());
    for (const char* id : {"game", "video", "text", "file"}) mode_->addItem(modeName(QLatin1String(id)), QLatin1String(id));
    mode_->setAccessibleName(QStringLiteral("모드 거르기"));
    top->addWidget(search_, 1);
    top->addWidget(mode_);
    layout->addLayout(top);
    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels({QStringLiteral("시각"), QStringLiteral("모드"), QStringLiteral("원문"), QStringLiteral("번역")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    table_->verticalHeader()->setVisible(false);
    table_->setWordWrap(true);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAccessibleName(QStringLiteral("번역 기록 목록"));
    layout->addWidget(table_, 1);
    auto* buttons = new QHBoxLayout;
    auto* copy = new QPushButton(QStringLiteral("선택 복사 (Ctrl+C)"));
    auto* copyAll = new QPushButton(QStringLiteral("번역 전체 복사"));
    auto* text = new QPushButton(QStringLiteral("TXT로 저장"));
    auto* srt = new QPushButton(QStringLiteral("SRT로 저장"));
    srt->setToolTip(QStringLiteral("영상 모드의 표시 시각으로 간이 자막 파일을 만듭니다. 영상 원본의 시간과 정확히 맞지는 않습니다."));
    auto* clear = new QPushButton(QStringLiteral("기록 지우기"));
    for (QPushButton* b : {copy, copyAll, text, srt}) buttons->addWidget(b);
    buttons->addStretch(1);
    buttons->addWidget(clear);
    layout->addLayout(buttons);
    note_ = new QLabel;
    note_->setWordWrap(true);
    layout->addWidget(note_);
    connect(search_, &QLineEdit::textChanged, this, &HistoryWindow::refresh);
    connect(mode_, &QComboBox::currentIndexChanged, this, &HistoryWindow::refresh);
    connect(copy, &QPushButton::clicked, this, &HistoryWindow::copySelection);
    auto* shortcut = new QShortcut(QKeySequence::Copy, table_);
    connect(shortcut, &QShortcut::activated, this, &HistoryWindow::copySelection);
    connect(copyAll, &QPushButton::clicked, this, [this] {
        QStringList lines;
        for (int index : visibleEntries()) lines << store_->entries().at(index).translation;
        QApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
        note_->setText(QStringLiteral("번역 %1줄을 복사했습니다.").arg(lines.size()));
    });
    connect(text, &QPushButton::clicked, this, [this] { exportTo(false); });
    connect(srt, &QPushButton::clicked, this, [this] { exportTo(true); });
    connect(clear, &QPushButton::clicked, this, [this] {
        if (QMessageBox::question(this, QStringLiteral("기록 지우기"), QStringLiteral("모든 번역 기록을 지울까요? 되돌릴 수 없습니다.")) ==
            QMessageBox::Yes) store_->clear();
    });
    connect(store_, &HistoryStore::changed, this, [this] { if (isVisible()) refresh(); });
    refresh();
}
QString HistoryWindow::filterText() const { return search_->text(); }
QList<int> HistoryWindow::visibleEntries() const {
    QList<int> rows;
    const QString needle = search_->text().trimmed();
    const QString mode = mode_->currentData().toString();
    const auto& entries = store_->entries();
    for (int i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (!mode.isEmpty() && e.mode != mode) continue;
        if (!needle.isEmpty() && !e.source.contains(needle, Qt::CaseInsensitive) &&
            !e.translation.contains(needle, Qt::CaseInsensitive)) continue;
        rows << i;
    }
    return rows;
}
void HistoryWindow::refresh() {
    const QList<int> rows = visibleEntries();
    table_->setRowCount(int(rows.size()));
    const auto& entries = store_->entries();
    for (int r = 0; r < rows.size(); ++r) {
        const auto& e = entries[rows[r]];
        const QStringList cells{e.when.toString(QStringLiteral("HH:mm:ss")), modeName(e.mode), e.source,
                                e.translation.isEmpty() ? QStringLiteral("(번역 없음)") : e.translation};
        for (int c = 0; c < cells.size(); ++c) {
            auto* item = table_->item(r, c);
            if (!item) { item = new QTableWidgetItem; table_->setItem(r, c, item); }
            item->setText(cells[c]);
            item->setData(Qt::UserRole, rows[r]);
        }
    }
    table_->resizeRowsToContents();
    if (!rows.isEmpty() && search_->text().isEmpty()) table_->scrollToBottom();
    const bool kept = QSettings().value(QStringLiteral("history/keep"), false).toBool();
    note_->setText(QStringLiteral("%1개 · %2").arg(rows.size()).arg(kept
        ? QStringLiteral("기록을 이 PC에 보관합니다 (설정 → 기록·로그에서 바꿀 수 있습니다).")
        : QStringLiteral("앱을 끄면 기록이 지워집니다. 보관하려면 설정 → 기록·로그에서 바꾸세요.")));
}
void HistoryWindow::copySelection() {
    QStringList lines;
    QList<int> done;
    for (const auto* item : table_->selectedItems()) {
        const int index = item->data(Qt::UserRole).toInt();
        if (done.contains(index)) continue;
        done << index;
        const auto& e = store_->entries().at(index);
        lines << e.source + QLatin1Char('\n') + e.translation;
    }
    if (lines.isEmpty()) { note_->setText(QStringLiteral("복사할 줄을 먼저 선택하세요.")); return; }
    QApplication::clipboard()->setText(lines.join(QStringLiteral("\n\n")));
    note_->setText(QStringLiteral("%1개를 복사했습니다 (원문과 번역).").arg(lines.size()));
}
void HistoryWindow::exportTo(bool srt) {
    QList<HistoryEntry> entries;
    for (int index : visibleEntries()) entries << store_->entries().at(index);
    if (entries.isEmpty()) { note_->setText(QStringLiteral("저장할 기록이 없습니다.")); return; }
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString name = QStringLiteral("번역기록-%1.%2").arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmm")),
                                                               srt ? QStringLiteral("srt") : QStringLiteral("txt"));
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("기록 저장"), folder + QLatin1Char('/') + name,
        srt ? QStringLiteral("자막 (*.srt)") : QStringLiteral("텍스트 (*.txt)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    const QString content = srt ? HistoryStore::toSrt(entries) : HistoryStore::toText(entries);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray("\xEF\xBB\xBF") + content.toUtf8()) < 0 || !file.commit()) {
        note_->setText(QStringLiteral("저장하지 못했습니다 · 폴더에 쓸 권한이 없거나 디스크가 가득 찼습니다 · 다른 폴더를 고르세요."));
        return;
    }
    note_->setText(QStringLiteral("저장했습니다: %1").arg(QDir::toNativeSeparators(path)));
}
