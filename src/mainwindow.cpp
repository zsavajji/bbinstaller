#include "mainwindow.h"

#include <QDragEnterEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSslSocket>
#include <QStyle>
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

bool isDark(const QPalette &p) { return p.color(QPalette::Window).lightness() < 128; }
QColor mutedColor(const QPalette &p) { return p.color(QPalette::PlaceholderText); }
QColor okColor(const QPalette &p) { return isDark(p) ? QColor(0x66bb6a) : QColor(0x2e7d32); }
QColor errColor(const QPalette &p) { return isDark(p) ? QColor(0xef5350) : QColor(0xc62828); }

// Secondary text: the platform's smaller size
void makeSmall(QWidget *w)
{
    QFont f = w->font();
    f.setPointSizeF(f.pointSizeF() * 0.85);
    w->setFont(f);
}

// tone: "muted", "ok", "err", or empty for normal text
void setTone(QLabel *l, const QString &tone)
{
    l->setProperty("tone", tone);
    const QPalette base = l->parentWidget() ? l->parentWidget()->palette() : QPalette();
    QPalette p = l->palette();
    p.setColor(QPalette::WindowText, tone == QLatin1String("muted") ? mutedColor(base)
                                     : tone == QLatin1String("ok")  ? okColor(base)
                                     : tone == QLatin1String("err") ? errColor(base)
                                                                    : base.color(QPalette::WindowText));
    l->setPalette(p);
}

} // namespace

DropArea::DropArea(QWidget *parent) : QFrame(parent)
{
    setAcceptDrops(true);
}

void DropArea::setHighlighted(bool on)
{
    if (m_highlighted == on) return;
    m_highlighted = on;
    update();
}

void DropArea::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
    p.fillPath(path, palette().color(QPalette::Base));
    QPen pen(m_highlighted ? palette().color(QPalette::Highlight) : palette().color(QPalette::Mid), m_highlighted ? 2 : 1.5, Qt::DashLine);
    p.setPen(pen);
    p.drawPath(path);
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

AddTarget::AddTarget(QWidget *parent) : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setMinimumHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void AddTarget::setText(const QString &text)
{
    m_text = text;
    update();
}

void AddTarget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setPen(m_hover ? palette().color(QPalette::Highlight) : mutedColor(palette()));
    p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, m_text);
}

void AddTarget::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
    if (onHover) onHover(true);
}

void AddTarget::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
    if (onHover) onHover(false);
}

void AddTarget::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint()) && onClicked) onClicked();
}

StatusDot::StatusDot(QWidget *parent) : QWidget(parent)
{
    setFixedSize(10, 10);
}

void StatusDot::setOn(bool on)
{
    m_on = on;
    update();
}

void StatusDot::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(m_on ? okColor(palette()) : mutedColor(palette()));
    p.drawEllipse(rect());
}

MainWindow::MainWindow()
{
    setWindowTitle(QStringLiteral("BB Installer"));
    resize(520, 560);
    setMinimumSize(420, 420);

    auto *body = new QVBoxLayout(this);

    m_deviceCard = new QGroupBox;
    auto *deviceRow = new QHBoxLayout(m_deviceCard);
    m_dot = new StatusDot;
    auto *deviceText = new QVBoxLayout;
    deviceText->setSpacing(0);
    m_model = new QLabel;
    QFont bold = m_model->font();
    bold.setBold(true);
    m_model->setFont(bold);
    m_detail = new QLabel;
    makeSmall(m_detail);
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
    scroll->viewport()->setAutoFillBackground(false);
    auto *content = new QWidget;
    content->setAutoFillBackground(false);
    m_list = new QVBoxLayout(content);
    m_list->setContentsMargins(8, 8, 8, 8);
    m_list->setSpacing(6);
    m_add = new AddTarget;
    QSizePolicy keep = m_add->sizePolicy();
    keep.setRetainSizeWhenHidden(true);
    m_add->setSizePolicy(keep);
    m_list->addWidget(m_add, 1);
    scroll->setWidget(content);
    dropLayout->addWidget(scroll);
    body->addWidget(m_drop, 1);

    auto *actions = new QHBoxLayout;
    m_password = new QLineEdit;
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setPlaceholderText(QStringLiteral("Device password (if set)"));
    m_install = new QPushButton(QStringLiteral("Install"));
    m_install->setObjectName(QStringLiteral("install"));
    m_install->setDefault(true);
    actions->addWidget(m_password, 1);
    actions->addWidget(m_install);
    body->addLayout(actions);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_status->hide();
    body->addWidget(m_status);

    m_add->onClicked = [this] {
        if (m_busy) return;
        const QStringList files = QFileDialog::getOpenFileNames(this, QString(), QString(), QStringLiteral("BlackBerry apps (*.bar)"));
        QList<bb::BarInfo> infos;
        for (const QString &f : files) infos << bb::barInfo(f);
        add(infos);
    };
    // The whole empty part of the drop area is the click target; light the border up with it
    m_add->onHover = [this](bool on) { m_drop->setHighlighted(on && !m_busy); };
    connect(m_drop, &DropArea::filesDropped, this, [this](const QStringList &paths) {
        if (m_busy) return;
        QList<bb::BarInfo> infos;
        for (const QString &f : paths) infos << bb::barInfo(f);
        add(infos);
    });
    connect(m_password, &QLineEdit::textChanged, this, &MainWindow::refresh);
    connect(m_password, &QLineEdit::returnPressed, m_install, &QPushButton::click);
    connect(m_install, &QPushButton::clicked, this, &MainWindow::install);

    setTone(m_detail, QStringLiteral("muted"));
    setTone(m_status, QStringLiteral("muted"));
    refresh();
    if (!QSslSocket::supportsSsl())
        setStatus(QStringLiteral("TLS support is missing (OpenSSL libraries not found), so the phone can't be reached."), true);
    QTimer::singleShot(0, this, &MainWindow::poll);
}

// Light/dark switches change the palette: recolour the labels that have a tone
void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange || e->type() == QEvent::StyleChange)
        QTimer::singleShot(0, this, &MainWindow::applyColors);
    QWidget::changeEvent(e);
}

void MainWindow::applyColors()
{
    for (QLabel *l : findChildren<QLabel *>())
        if (l->property("tone").isValid()) setTone(l, l->property("tone").toString());
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
    setTone(m_status, error ? QStringLiteral("err") : QStringLiteral("muted"));
}

void MainWindow::refresh()
{
    const bb::DeviceInfo *d = m_device ? &m_device->info() : nullptr;
    m_dot->setOn(d != nullptr);
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
        bar->el->setFrameShape(QFrame::StyledPanel);
        auto *grid = new QGridLayout(bar->el);
        grid->setContentsMargins(10, 8, 6, 8);
        grid->setHorizontalSpacing(10);
        grid->setVerticalSpacing(4);
        auto *name = new ElidedLabel(info.name);
        name->setToolTip(info.path);
        bar->version = new QLabel(info.version);
        auto *remove = new QToolButton;
        remove->setIcon(style()->standardIcon(QStyle::SP_LineEditClearButton));
        remove->setAutoRaise(true);
        remove->setToolTip(QStringLiteral("Remove"));
        bar->detail = new QLabel(info.error.isEmpty() ? mb(info.size) : info.error);
        bar->detail->setWordWrap(true);
        makeSmall(bar->detail);
        bar->progress = new QProgressBar;
        bar->progress->setRange(0, 100);
        bar->progress->setTextVisible(false);
        bar->progress->hide();
        grid->addWidget(name, 0, 0);
        grid->addWidget(bar->version, 0, 1);
        grid->addWidget(remove, 0, 2);
        grid->addWidget(bar->detail, 1, 0, 1, 3);
        grid->addWidget(bar->progress, 2, 0, 1, 3);
        grid->setColumnStretch(0, 1);
        m_list->insertWidget(m_list->count() - 1, bar->el);
        setTone(bar->version, QStringLiteral("muted"));
        setTone(bar->detail, info.error.isEmpty() ? QStringLiteral("muted") : QStringLiteral("err"));

        Bar *raw = bar.get();
        connect(remove, &QToolButton::clicked, this, [this, raw] {
            if (m_busy) return;
            raw->el->deleteLater();
            m_bars.erase(std::find_if(m_bars.begin(), m_bars.end(), [&](const auto &b) { return b.get() == raw; }));
            refresh();
        });
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
                setTone(bar->detail, QStringLiteral("ok"));
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
