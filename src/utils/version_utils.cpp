module;
#include <QVersionNumber>

module raad.utils.version_utils;

namespace raad::utils {

struct ParsedVersion {
    QVersionNumber base;
    QString suffix;
    bool prerelease = false;
};

static ParsedVersion parseVersion(const QString& input)
{
    ParsedVersion out;
    QString v = input.trimmed();
    if (v.startsWith('v') || v.startsWith('V')) v = v.mid(1);
    const int dash = v.indexOf('-');
    if (dash >= 0) {
        out.suffix = v.mid(dash + 1).trimmed();
        out.prerelease = !out.suffix.isEmpty();
        v = v.left(dash);
    }
    out.base = QVersionNumber::fromString(v);
    return out;
}

int compareVersions(const QString& a, const QString& b)
{
    const ParsedVersion va = parseVersion(a);
    const ParsedVersion vb = parseVersion(b);
    const int baseCmp = QVersionNumber::compare(va.base, vb.base);
    if (baseCmp != 0) return baseCmp < 0 ? -1 : 1;
    if (va.prerelease == vb.prerelease) {
        const int sufCmp = QString::compare(va.suffix, vb.suffix, Qt::CaseInsensitive);
        if (sufCmp == 0) return 0;
        return sufCmp < 0 ? -1 : 1;
    }
    // prerelease is considered lower than stable
    return va.prerelease ? -1 : 1;
}

} // namespace raad::utils
