#pragma once

#include "bb.h"

#include <QFrame>
#include <QNetworkAccessManager>
#include <QWidget>
#include <memory>
#include <vector>

class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

// The dashed area .bar files are dropped on
class DropArea : public QFrame {
    Q_OBJECT
public:
    explicit DropArea(QWidget *parent = nullptr);
    void setHighlighted(bool on);

signals:
    void filesDropped(const QStringList &paths);

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;
};

class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow();

protected:
    void changeEvent(QEvent *e) override;
    bool eventFilter(QObject *o, QEvent *e) override;

private:
    struct Bar {
        bb::BarInfo info;
        QFrame *el;
        QLabel *detail;
        QProgressBar *progress;
        bool done = false;
    };

    void poll();
    void refresh();
    void add(const QList<bb::BarInfo> &infos);
    void install();
    void setStatus(const QString &text, bool error = false);
    void applyTheme();
    QList<Bar *> valid() const;

    QNetworkAccessManager m_nam;
    std::unique_ptr<bb::Device> m_device;
    std::vector<std::unique_ptr<Bar>> m_bars;
    QList<Bar *> m_queue;
    bool m_busy = false;
    bool m_dark = false;

    QFrame *m_deviceCard;
    QLabel *m_dot;
    QLabel *m_model;
    QLabel *m_detail;
    DropArea *m_drop;
    QVBoxLayout *m_list;
    QPushButton *m_add;
    QLineEdit *m_password;
    QPushButton *m_install;
    QLabel *m_status;
};
