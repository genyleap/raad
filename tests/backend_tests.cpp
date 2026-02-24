#include <QtTest/QtTest>

import raad.utils.version_utils;
import raad.utils.download_utils;
import raad.utils.category_utils;

namespace utils = raad::utils;

class BackendTests final : public QObject
{
    Q_OBJECT

private slots:
    void compareVersions_data();
    void compareVersions();
    void detectChecksumAlgo();
    void normalizeHost();
    void detectCategory();
};

void BackendTests::compareVersions_data()
{
    QTest::addColumn<QString>("lhs");
    QTest::addColumn<QString>("rhs");
    QTest::addColumn<int>("expected");

    QTest::newRow("stable_gt_prerelease") << QStringLiteral("1.2.3") << QStringLiteral("1.2.3-beta.1") << 1;
    QTest::newRow("stable_lt_stable") << QStringLiteral("1.2.3") << QStringLiteral("1.2.4") << -1;
    QTest::newRow("same") << QStringLiteral("v2.0.0") << QStringLiteral("2.0.0") << 0;
}

void BackendTests::compareVersions()
{
    QFETCH(QString, lhs);
    QFETCH(QString, rhs);
    QFETCH(int, expected);
    QCOMPARE(utils::compareVersions(lhs, rhs), expected);
}

void BackendTests::detectChecksumAlgo()
{
    QCOMPARE(utils::detectChecksumAlgo(QStringLiteral("d41d8cd98f00b204e9800998ecf8427e")), QStringLiteral("MD5"));
    QCOMPARE(utils::detectChecksumAlgo(QStringLiteral("a9993e364706816aba3e25717850c26c9cd0d89d")), QStringLiteral("SHA1"));
    QCOMPARE(utils::detectChecksumAlgo(QStringLiteral("e3b0c44298fc1c149afbf4c8996fb924"
                                                      "27ae41e4649b934ca495991b7852b855")),
             QStringLiteral("SHA256"));
}

void BackendTests::normalizeHost()
{
    QCOMPARE(utils::normalizeHost(QStringLiteral("https://Example.com/path?q=1")), QStringLiteral("example.com"));
    QCOMPARE(utils::normalizeHost(QStringLiteral("CDN.EXAMPLE.COM:443")), QStringLiteral("cdn.example.com:443"));
}

void BackendTests::detectCategory()
{
    QCOMPARE(utils::detectCategory(QStringLiteral("movie.mkv")), QStringLiteral("Video"));
    QCOMPARE(utils::detectCategory(QStringLiteral("archive.tar.gz")), QStringLiteral("Archives"));
    QCOMPARE(utils::detectCategory(QStringLiteral("unknown.customext")), QStringLiteral("Other"));
}

QTEST_MAIN(BackendTests)
#include "backend_tests.moc"
