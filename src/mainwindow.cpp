#include "mainwindow.h"

#include <QDragEnterEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSslSocket>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

// A label that ends in "…" instead of widening the window
class ElidedLabel : public QLabel {
public:
    using QLabel::QLabel;
    QSize minimumSizeHint() const override { return {0, QLabel::minimumSizeHint().height()}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(palette().color(foregroundRole()));
        p.drawText(contentsRect(), Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(text(), Qt::ElideRight, contentsRect().width()));
    }
};

QString mb(qint64 n) { return QString::number(n / 1e6, 'f', 1) + QStringLiteral(" MB"); }

// Re-evaluate [property=...] selectors after a property change
void setStyleProperty(QWidget *w, const char *name, const QVariant &value)
{
    w->setProperty(name, value);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

bool systemIsDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const Qt::ColorScheme scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown) return scheme == Qt::ColorScheme::Dark;
#endif
    return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

} // namespace

DropArea::DropArea(QWidget *parent) : QFrame(parent)
{
    setObjectName(QStringLiteral("drop"));
    setAcceptDrops(true);
}

void DropArea::setHighlighted(bool on)
{
    if (property("over").toBool() != on) setStyleProperty(this, "over", on);
}

void DropArea::dragEnterEvent(QDragEnterEvent *e)
{
    if (!e->mimeData()->hasUrls()) return;
    e->acceptProposedAction();
    setHighlighted(true);
}

void DropArea::dragLeaveEvent(QDragLeaveEvent *)
{
    setHighlighted(false);
}

void DropArea::dropEvent(QDropEvent *e)
{
    setHighlighted(false);
    QStringList paths;
    for (const QUrl &url : e->mimeData()->urls())
        if (url.isLocalFile()) paths << url.toLocalFile();
    e->acceptProposedAction();
    emit filesDropped(paths);
}

MainWindow::MainWindow()
{
    setObjectName(QStringLiteral("root"));
    setWindowTitle(QStringLiteral("BB Installer"));
    resize(520, 560);
    setMinimumSize(420, 420);

    auto *body = new QVBoxLayout(this);
    body->setContentsMargins(20, 20, 20, 20);
    body->setSpacing(14);

    m_deviceCard = new QFrame;
    m_deviceCard->setObjectName(QStringLiteral("device"));
    auto *deviceRow = new QHBoxLayout(m_deviceCard);
    deviceRow->setContentsMargins(14, 12, 14, 12);
    deviceRow->setSpacing(10);
    m_dot = new QLabel;
    m_dot->setObjectName(QStringLiteral("dot"));
    m_dot->setFixedSize(10, 10);
    auto *deviceText = new QVBoxLayout;
    deviceText->setSpacing(0);
    m_model = new QLabel;
    m_model->setObjectName(QStringLiteral("model"));
    m_detail = new QLabel;
    m_detail->setProperty("role", QStringLiteral("small"));
    deviceText->addWidget(m_model);
    deviceText->addWidget(m_detail);
    deviceRow->addWidget(m_dot);
    deviceRow->addLayout(deviceText, 1);
    body->addWidget(m_deviceCard);

    m_drop = new DropArea;
    m_drop->setMinimumHeight(120);
    auto *dropLayout = new QVBoxLayout(m_drop);
    dropLayout->setContentsMargins(2, 2, 2, 2);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("list"));
    m_list = new QVBoxLayout(content);
    m_list->setContentsMargins(8, 8, 8, 8);
    m_list->setSpacing(6);
    m_add = new QPushButton;
    m_add->setObjectName(QStringLiteral("add"));
    m_add->setCursor(Qt::PointingHandCursor);
    m_add->setMinimumHeight(48);
    m_add->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QSizePolicy keep = m_add->sizePolicy();
    keep.setRetainSizeWhenHidden(true);
    m_add->setSizePolicy(keep);
    m_add->installEventFilter(this);
    m_list->addWidget(m_add, 1);
    scroll->setWidget(content);
    dropLayout->addWidget(scroll);
    body->addWidget(m_drop, 1);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(10);
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(QStringLiteral("Device password (if set)"));
    m_install = new QPushButton(QStringLiteral("Install"));
    m_install->setObjectName(QStringLiteral("install"));
    m_install->setCursor(Qt::PointingHandCursor);
    actions->addWidget(m_password, 1);
    actions->addWidget(m_install);
    body->addLayout(actions);

    m_status = new QLabel;
    m_status->setObjectName(QStringLiteral("status"));
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->hide();
    body->addWidget(m_status);

    connect(m_add, &QPushButton::clicked, this, [this] {
        if (m_busy) return;
        const QStringList files = QFileDialog::getOpenFileNames(this, QString(), QString(), QStringLiteral("BlackBerry apps (*.bar)"));
        QList<bb::BarInfo> infos;
        for (const QString &f : files) infos << bb::barInfo(f);
        add(infos);
    });
    connect(m_drop, &DropArea::filesDropped, this, [this](const QStringList &paths) {
        if (m_busy) return;
        QList<bb::BarInfo> infos;
        for (const QString &f : paths) infos << bb::barInfo(f);
        add(infos);
    });
    connect(m_password, &QLineEdit::textChanged, this, &MainWindow::refresh);
    connect(m_password, &QLineEdit::returnPressed, m_install, &QPushButton::click);
    connect(m_install, &QPushButton::clicked, this, &MainWindow::install);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &MainWindow::applyTheme);
#endif

    applyTheme();
    refresh();
    if (!QSslSocket::supportsSsl())
        setStatus(QStringLiteral("TLS support is missing (OpenSSL libraries not found), so the phone can't be reached."), true);
    QTimer::singleShot(0, this, &MainWindow::poll);
}

void MainWindow::applyTheme()
{
    m_dark = systemIsDark();
    struct Palette { const char *bg, *panel, *text, *muted, *line, *accent, *ok, *err; };
    const Palette c = m_dark ? Palette{"#18181a", "#222225", "#ececea", "#9a9a96", "#36363a", "#4c8dff", "#66bb6a", "#ef5350"}
                             : Palette{"#f6f6f4", "#ffffff", "#1d1d1b", "#6b6b66", "#dcdcd6", "#1f6feb", "#2e7d32", "#c62828"};
    // Stand-in for CSS opacity: .45 on a disabled Install button
    const auto fade = [&](const char *fg) {
        const QColor a(fg), b(c.bg);
        return QColor::fromRgbF(a.redF() * .45 + b.redF() * .55, a.greenF() * .45 + b.greenF() * .55, a.blueF() * .45 + b.blueF() * .55).name();
    };
    setStyleSheet(QStringLiteral(R"(
        QWidget { font-size: 14px; }
        QWidget#root { background: %1; color: %3; }
        QLabel { color: %3; background: transparent; }
        QLabel[role="small"] { color: %4; font-size: 12px; }
        QLabel[role="small"][state="bad"] { color: %8; }
        QLabel[role="small"][state="done"] { color: %7; }
        QLabel[role="ver"] { color: %4; }
        QLabel#model { font-weight: bold; }
        QFrame#device { background: %2; border: 1px solid %5; border-radius: 10px; }
        QLabel#dot { background: %4; border-radius: 5px; }
        QLabel#dot[on="true"] { background: %7; }
        QFrame#drop { background: %2; border: 2px dashed %5; border-radius: 10px; }
        QFrame#drop[over="true"] { border-color: %6; }
        QScrollArea, QWidget#list { background: transparent; border: 0; }
        QPushButton#add { background: transparent; border: 0; color: %4; }
        QPushButton#add:hover { color: %6; }
        QFrame[role="bar"] { border: 1px solid %5; border-radius: 8px; background: transparent; }
        QToolButton[role="remove"] { border: 0; background: transparent; color: %4; font-size: 16px; padding: 0 4px; }
        QProgressBar { border: 0; border-radius: 3px; background: %5; max-height: 6px; min-height: 6px; }
        QProgressBar::chunk { background: %6; border-radius: 3px; }
        QLineEdit { padding: 9px 12px; border-radius: 8px; border: 1px solid %5; background: %2; color: %3; }
        QLineEdit:disabled { color: %4; }
        QPushButton#install { padding: 9px 12px; border-radius: 8px; border: 1px solid %6; background: %6; color: #fff; font-weight: 600; }
        QPushButton#install:disabled { border-color: %9; background: %9; color: %10; }
        QLabel#status { color: %4; }
        QLabel#status[err="true"] { color: %8; }
    )")
                      .arg(c.bg, c.panel, c.text, c.muted, c.line, c.accent, c.ok, c.err, fade(c.accent))
                      .arg(fade("#ffffff")));
}

bool MainWindow::eventFilter(QObject *o, QEvent *e)
{
    // The whole empty part of the drop area is the "add" button; light the border up with it
    if (o == m_add && (e->type() == QEvent::Enter || e->type() == QEvent::Leave))
        m_drop->setHighlighted(e->type() == QEvent::Enter && !m_busy);
    return QWidget::eventFilter(o, e);
}

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::ApplicationPaletteChange && systemIsDark() != m_dark) applyTheme();
    QWidget::changeEvent(e);
}

QList<MainWindow::Bar *> MainWindow::valid() const
{
    QList<Bar *> out;
    for (const auto &b : m_bars)
        if (b->info.error.isEmpty() && !b->done) out << b.get();
    return out;
}

void MainWindow::setStatus(const QString &text, bool error)
{
    m_status->setText(text);
    m_status->setVisible(!text.isEmpty());
    setStyleProperty(m_status, "err", error);
}

void MainWindow::refresh()
{
    const bb::DeviceInfo *d = m_device ? &m_device->info() : nullptr;
    setStyleProperty(m_dot, "on", d != nullptr);
    m_model->setText(d ? d->model : QStringLiteral("No phone connected"));
    if (d) {
        QStringList parts;
        if (!d->os.isEmpty()) parts << QStringLiteral("OS ") + d->os;
        parts << d->ip;
        if (!d->setupDone) parts << QStringLiteral("finish setup on the phone first");
        m_detail->setText(parts.join(QStringLiteral(" · ")));
    } else {
        m_detail->setText(QStringLiteral("Plug in your BlackBerry 10 with a USB cable"));
    }
    m_install->setEnabled(!m_busy && d && !valid().isEmpty());
    m_add->setText(m_bars.empty() ? QStringLiteral("Drop .bar files here, or click to choose")
                                  : QStringLiteral("+  Drop more .bar files, or click to add"));
    m_add->setVisible(!m_busy);
    m_password->setEnabled(!m_busy);
}

// Watch for the USB network link every 2 s; only talk to the phone when a new link shows up,
// since repeated discovery makes it close port 80. Paused while installing.
void MainWindow::poll()
{
    const QStringList ips = bb::candidateIps();
    if (!m_busy && !(m_device && ips.contains(m_device->info().ip))) {
        std::optional<bb::DeviceInfo> found;
        for (const QString &ip : ips)
            if ((found = bb::identify(m_nam, ip))) break;
        if (found || m_device) {
            m_device = found ? std::make_unique<bb::Device>(*found) : nullptr;
            refresh();
        }
    }
    QTimer::singleShot(2000, this, &MainWindow::poll);
}

void MainWindow::add(const QList<bb::BarInfo> &infos)
{
    for (const bb::BarInfo &info : infos) {
        if (std::any_of(m_bars.begin(), m_bars.end(), [&](const auto &b) { return b->info.path == info.path; })) continue;
        auto bar = std::make_unique<Bar>();
        bar->info = info;
        bar->el = new QFrame;
        bar->el->setProperty("role", QStringLiteral("bar"));
        auto *grid = new QGridLayout(bar->el);
        grid->setContentsMargins(10, 8, 10, 8);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(4);
        auto *name = new ElidedLabel(info.name);
        name->setProperty("role", QStringLiteral("name"));
        name->setToolTip(info.path);
        auto *ver = new QLabel(info.version);
        ver->setProperty("role", QStringLiteral("ver"));
        auto *remove = new QToolButton;
        remove->setText(QStringLiteral("×"));
        remove->setToolTip(QStringLiteral("Remove"));
        remove->setProperty("role", QStringLiteral("remove"));
        remove->setCursor(Qt::PointingHandCursor);
        bar->detail = new QLabel(info.error.isEmpty() ? mb(info.size) : info.error);
        bar->detail->setProperty("role", QStringLiteral("small"));
        bar->detail->setProperty("state", info.error.isEmpty() ? QString() : QStringLiteral("bad"));
        bar->detail->setWordWrap(true);
        bar->progress = new QProgressBar;
        bar->progress->setRange(0, 100);
        bar->progress->setTextVisible(false);
        bar->progress->hide();
        grid->addWidget(name, 0, 0);
        grid->addWidget(ver, 0, 1);
        grid->addWidget(remove, 0, 2);
        grid->addWidget(bar->detail, 1, 0, 1, 3);
        grid->addWidget(bar->progress, 2, 0, 1, 3);
        grid->setColumnStretch(0, 1);

        Bar *raw = bar.get();
        connect(remove, &QToolButton::clicked, this, [this, raw] {
            if (m_busy) return;
            raw->el->deleteLater();
            m_bars.erase(std::find_if(m_bars.begin(), m_bars.end(), [&](const auto &b) { return b.get() == raw; }));
            refresh();
        });
        m_list->insertWidget(m_list->count() - 1, bar->el);
        m_bars.push_back(std::move(bar));
    }
    refresh();
}

void MainWindow::install()
{
    m_queue = valid();
    if (m_queue.isEmpty()) return;
    m_busy = true;
    m_drop->setHighlighted(false);
    refresh();
    setStatus(QStringLiteral("Installing %1 app%2…").arg(m_queue.size()).arg(m_queue.size() > 1 ? QStringLiteral("s") : QString()));
    try {
        if (!m_device) throw bb::Error{QStringLiteral("No phone connected")};
        QStringList paths;
        for (Bar *b : m_queue) paths << b->info.path;
        m_device->login(m_password->text());
        m_device->install(paths, [this](const bb::Progress &p) {
            Bar *bar = m_queue[p.index];
            bar->progress->show();
            bar->progress->setValue(p.percent);
            bar->detail->setText(p.phase == bb::Progress::Upload    ? QStringLiteral("Sending… %1%").arg(p.percent)
                                 : p.phase == bb::Progress::Install ? QStringLiteral("Installing… %1%").arg(p.percent)
                                                                    : QStringLiteral("Installed"));
            if (p.phase == bb::Progress::Done) {
                bar->done = true;
                setStyleProperty(bar->detail, "state", QStringLiteral("done"));
                bar->progress->hide();
            }
        });
        setStatus(QStringLiteral("Done."));
    } catch (const bb::Error &e) {
        setStatus(e.message, true);
        // A wrong password costs an attempt: make the user retype before trying again
        if (e.wrongPassword) m_password->clear();
        for (Bar *b : m_queue)
            if (!b->done) b->progress->hide();
    }
    m_busy = false;
    refresh();
}
