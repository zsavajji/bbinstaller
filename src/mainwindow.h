#pragma once

#include "bb.h"

#include <QFrame>
#include <QNetworkAccessManager>
#include <QWidget>
#include <functional>
#include <memory>
#include <vector>

class QGroupBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QVBoxLayout;

// The dashed area .bar files are dropped on, drawn with the system palette
class DropArea : public QFrame {
    Q_OBJECT
public:
    explicit DropArea(QWidget *parent = nullptr);
    void setHighlighted(bool on);

signals:
    void filesDropped(const QStringList &paths);

protected:
    void paintEvent(QPaintEvent *e) override;
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *e) override;
    void dropEvent(QDropEvent *e) override;

private:
    bool m_highlighted = false;
};

// The empty part of the drop area: shows a hint and opens the file picker when clicked
class AddTarget : public QWidget {
public:
    explicit AddTarget(QWidget *parent = nullptr);
    void setText(const QString &text);
    std::function<void()> onClicked;
    std::function<void(bool)> onHover;

protected:
    void paintEvent(QPaintEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    QString m_text;
    bool m_hover = false;
};

class StatusDot : public QWidget {
public:
    explicit StatusDot(QWidget *parent = nullptr);
    void setOn(bool on);

protected:
    void paintEvent(QPaintEvent *e) override;

private:
    bool m_on = false;
};

class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow();

protected:
    void changeEvent(QEvent *e) override;

private:
    struct Bar {
        bb::BarInfo info;
        QFrame *el;
        QLabel *version;
        QLabel *detail;
        QProgressBar *progress;
        bool done = false;
    };

    void poll();
    void refresh();
    void add(const QList<bb::BarInfo> &infos);
    void install();
    void setStatus(const QString &text, bool error = false);
    void applyColors();
    QList<Bar *> valid() const;

    QNetworkAccessManager m_nam;
    std::unique_ptr<bb::Device> m_device;
    std::vector<std::unique_ptr<Bar>> m_bars;
    QList<Bar *> m_queue;
    bool m_busy = false;

    QGroupBox *m_deviceCard;
    StatusDot *m_dot;
    QLabel *m_model;
    QLabel *m_detail;
    DropArea *m_drop;
    QVBoxLayout *m_list;
    AddTarget *m_add;
    QLineEdit *m_password;
    QPushButton *m_install;
    QLabel *m_status;
};
