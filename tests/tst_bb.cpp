#include "bb.h"

#include <QTemporaryDir>
#include <QTest>

class TestBb : public QObject {
    Q_OBJECT
private slots:
    // A wrong hash costs a password attempt on the phone, so pin it to Sachesi's algorithm.
    // Vector from a line-for-line C mirror of InstallNet::HashPass (src/installer.cpp:1531).
    void hashPass()
    {
        QCOMPARE(bb::hashPass(QStringLiteral("hunter2"), "ABCDEF0123456789", QByteArray::fromHex("deadbeef01020304"), 1000).toHex().toUpper(),
                 QByteArray("CDB08C3197BA5BCA3BA0EF335194995A78FF564B7CF7FCC3C697BC483405B457B910DCAFEBABB593596DA67F2BBE5B8E02B332760AE84D67D5A3C2FF05E1EEC3"));
    }

    void barInfoRejectsNonZip()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("junk.bar"));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("not a zip");
        f.close();
        const bb::BarInfo info = bb::barInfo(path);
        QCOMPARE(info.name, QStringLiteral("junk.bar"));
        QCOMPARE(info.error, QStringLiteral("not an installable .bar (not a zip file)"));
    }

    // Stored and deflated manifests, plus the OS/radio refusal
    void barInfoReadsManifest_data()
    {
        QTest::addColumn<QString>("fixture");
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("version");
        QTest::addColumn<QString>("error");
        QTest::newRow("deflated") << "app-deflated.bar" << "com.example.app" << "1.2.3.4" << "";
        QTest::newRow("stored") << "app-stored.bar" << "com.example.app" << "1.2.3.4" << "";
        QTest::newRow("system") << "os.bar" << "qc8960.os" << "10.3.3.3216" << "OS/radio bar: use Sachesi";
    }

    void barInfoReadsManifest()
    {
        QFETCH(QString, fixture);
        const bb::BarInfo info = bb::barInfo(QFINDTESTDATA(QStringLiteral("data/") + fixture));
        QFETCH(QString, name);
        QFETCH(QString, version);
        QFETCH(QString, error);
        QCOMPARE(info.name, name);
        QCOMPARE(info.version, version);
        QCOMPARE(info.error, error);
    }
};

QTEST_GUILESS_MAIN(TestBb)
#include "tst_bb.moc"
