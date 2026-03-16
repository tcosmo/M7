#include "VerovioEngine.h"

#include <QPainter>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCoreApplication>
#include <QDir>
#include <QDomDocument>

#include "toolkit.h"

namespace scoretracker {

// Base page width in Verovio units (matching the default ~A4 width)
static const double VEROVIO_PAGE_BASE = 2100.0;

VerovioEngine::VerovioEngine()
{
    m_toolkit = new vrv::Toolkit(false);

    // Set resource path — look relative to the executable or in thirdparty
    QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates = {
        appDir + "/../../thirdparty/verovio/data",
        appDir + "/../share/verovio/data",
        appDir + "/verovio/data",
    };
    for (const QString& path : candidates) {
        QDir dir(path);
        if (dir.exists()) {
            m_toolkit->SetResourcePath(dir.absolutePath().toStdString());
            qDebug() << "Verovio resources:" << dir.absolutePath();
            break;
        }
    }
}

VerovioEngine::~VerovioEngine()
{
    delete m_toolkit;
}

bool VerovioEngine::loadMusicXML(const QString& path)
{
    QFileInfo fi(path);
    if (!fi.exists()) {
        qWarning() << "MusicXML file not found:" << path;
        return false;
    }

    m_musicXmlPath = path;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "Failed to open MusicXML:" << path;
        return false;
    }
    m_musicXmlData = QString::fromUtf8(file.readAll());
    file.close();

    parseParts(m_musicXmlData);
    applyOptions();

    bool ok = m_toolkit->LoadData(m_musicXmlData.toStdString());
    if (!ok) {
        qWarning() << "Verovio failed to load MusicXML:" << path;
        return false;
    }

    // Force MIDI computation so GetMIDIValuesForElement works for all notes
    try { m_toolkit->RenderToMIDI(); } catch (...) {}

    buildTimemap();

    qDebug() << "Verovio loaded:" << path
             << "pages:" << m_toolkit->GetPageCount()
             << "parts:" << m_parts.size();
    return true;
}

void VerovioEngine::setPageSizeInches(double width, double height)
{
    m_pageWidthIn = width;
    m_pageHeightIn = height;
}

void VerovioEngine::setMarginsInches(double top, double bottom, double left, double right)
{
    m_marginTop = top;
    m_marginBottom = bottom;
    m_marginLeft = left;
    m_marginRight = right;
}

void VerovioEngine::setSpatiumInches(double sp)
{
    m_spatiumIn = sp;
}

void VerovioEngine::setLayoutMode(int mode)
{
    m_layoutMode = mode;
}

void VerovioEngine::setShowTitleFrame(bool show)
{
    m_showTitle = show;
}

void VerovioEngine::applyOptions()
{
    if (!m_toolkit) return;

    double dpi = internalDPI();

    int pageW = static_cast<int>(m_pageWidthIn * dpi);
    int mTop = static_cast<int>(m_marginTop * dpi);
    int mBottom = static_cast<int>(m_marginBottom * dpi);
    int mLeft = static_cast<int>(m_marginLeft * dpi);
    int mRight = static_cast<int>(m_marginRight * dpi);

    // Matching the electron branch approach:
    // - Very tall single page (continuous vertical scroll, no page breaks)
    // - scale=40, adjustPageHeight=true
    // - pageWidth derived from desired physical width
    int tallPageH = 60000;

    QString options = QString(
        R"({
            "pageWidth": %1,
            "pageHeight": %2,
            "pageMarginTop": %3,
            "pageMarginBottom": %4,
            "pageMarginLeft": %5,
            "pageMarginRight": %6,
            "scale": %7,
            "svgViewBox": true,
            "adjustPageHeight": true,
            "breaks": "auto",
            "header": "none",
            "footer": "none"
        })")
        .arg(pageW)
        .arg(tallPageH)
        .arg(mTop)
        .arg(mBottom)
        .arg(mLeft)
        .arg(mRight)
        .arg(m_scale);

    m_toolkit->SetOptions(options.toStdString());
}

void VerovioEngine::layout()
{
    if (!m_toolkit) return;
    applyOptions();
    m_toolkit->RedoLayout();
    buildTimemap();
}

int VerovioEngine::pageCount() const
{
    if (!m_toolkit) return 0;
    return m_toolkit->GetPageCount();
}

QSizeF VerovioEngine::pageSize(int pageIndex) const
{
    Q_UNUSED(pageIndex);
    double dpi = internalDPI();
    return QSizeF(m_pageWidthIn * dpi, m_pageHeightIn * dpi);
}

double VerovioEngine::internalDPI() const
{
    return VEROVIO_PAGE_BASE / m_pageWidthIn;
}

void VerovioEngine::renderPage(int pageIndex, QPainter& painter)
{
    // Verovio rendering is done via QWebEngineView (usesWebRendering() == true).
    // This method is a no-op; the web view handles display.
    Q_UNUSED(pageIndex);
    Q_UNUSED(painter);
}

QString VerovioEngine::renderAllPagesHtml() const
{
    if (!m_toolkit) return QString();

    int n = m_toolkit->GetPageCount();

    // Render all SVGs once and cache them (getNotesForPart needs the same IDs)
    m_renderedSvgs.clear();
    for (int i = 1; i <= n; ++i) {
        QString svgStr = QString::fromStdString(m_toolkit->RenderToSVG(i));
        svgStr.replace(QStringLiteral("overflow=\"visible\""),
                        QStringLiteral("overflow=\"hidden\""));
        m_renderedSvgs.append(svgStr);
    }

    // Build HTML by concatenation — NOT QString::arg() —
    // because Verovio SVG contains '%' characters that arg() would corrupt.
    QString html;
    html += QStringLiteral(
        "<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"utf-8\">\n"
        "<style>\n"
        "body { margin: 0; padding: 0; background: #3a3a3a; }\n"
        ".page { background: white; }\n"
        ".page svg { width: 100%; height: auto; display: block; }\n"
        "</style>\n"
        "<script>\n"
        "var HL = {\n"
        "  'highlight-v0': {rc:'hl-v0',\n"
        "    style:'fill:rgba(50,120,255,0.15);stroke:rgb(50,120,255);stroke-width:45'},\n"
        "  'highlight-v1': {rc:'hl-v1',\n"
        "    style:'fill:rgba(180,80,220,0.15);stroke:rgb(180,80,220);stroke-width:45'}\n"
        "};\n"
        "function highlightNotes(ids, cls) {\n"
        "  var c = HL[cls] || HL['highlight-v1'];\n"
        "  document.querySelectorAll('.'+c.rc).forEach(function(e){e.remove();});\n"
        "  ids.forEach(function(id) {\n"
        "    var el = document.getElementById(id);\n"
        "    if (!el) return;\n"
        "    var bb = el.getBBox();\n"
        "    var px = 100, py = 120;\n"
        "    var r = document.createElementNS('http://www.w3.org/2000/svg','rect');\n"
        "    r.setAttribute('x', bb.x - px);\n"
        "    r.setAttribute('y', bb.y - py);\n"
        "    r.setAttribute('width', bb.width + px*2);\n"
        "    r.setAttribute('height', bb.height + py*2);\n"
        "    r.setAttribute('rx', '60');\n"
        "    r.setAttribute('ry', '60');\n"
        "    r.style.cssText = c.style;\n"
        "    r.setAttribute('class', c.rc);\n"
        "    el.parentNode.insertBefore(r, el);\n"
        "  });\n"
        "}\n"
        "function clearHighlights(cls) {\n"
        "  var c = HL[cls] || HL['highlight-v1'];\n"
        "  document.querySelectorAll('.'+c.rc).forEach(function(e){e.remove();});\n"
        "}\n"
        "var _maxScrollTarget = 0;\n"
        "// Single-voice: scroll when next highlight leaves viewport\n"
        "function autoScrollSingle() {\n"
        "  var el = document.querySelector('.hl-v0');\n"
        "  if (!el) return;\n"
        "  var r = el.getBoundingClientRect();\n"
        "  var vpH = window.innerHeight;\n"
        "  var margin = 60;\n"
        "  if (r.top >= margin && r.bottom <= vpH - margin) return;\n"
        "  var dest = window.scrollY + r.top - margin;\n"
        "  if (dest <= _maxScrollTarget + 5) return;\n"
        "  _maxScrollTarget = dest;\n"
        "  window.scrollTo({top: Math.max(0, dest), behavior: 'smooth'});\n"
        "}\n"
        "// Multi-voice: only scroll when a PLAYED note is off-screen.\n"
        "// playedIds = element IDs of notes just played.\n"
        "function autoScrollMulti(playedIds) {\n"
        "  if (!playedIds || !playedIds.length) return;\n"
        "  var vpH = window.innerHeight;\n"
        "  var margin = 60;\n"
        "  // Are all played notes visible? If so, no scroll.\n"
        "  var anyOffScreen = false;\n"
        "  for (var i = 0; i < playedIds.length; i++) {\n"
        "    var el = document.getElementById(playedIds[i]);\n"
        "    if (!el) continue;\n"
        "    var r = el.getBoundingClientRect();\n"
        "    if (r.top < 0 || r.bottom > vpH) { anyOffScreen = true; break; }\n"
        "  }\n"
        "  if (!anyOffScreen) return;\n"
        "  // A played note is off-screen. Gather all current highlights.\n"
        "  var allEls = document.querySelectorAll('.hl-v0,.hl-v1');\n"
        "  var minY = Infinity, maxY = -Infinity;\n"
        "  for (var i = 0; i < allEls.length; i++) {\n"
        "    var cy = allEls[i].getBoundingClientRect().top;\n"
        "    if (cy < minY) minY = cy;\n"
        "    if (cy > maxY) maxY = cy;\n"
        "  }\n"
        "  // Try to fit all voices\n"
        "  var dest = window.scrollY + minY - margin;\n"
        "  if (maxY - minY + 2*margin > vpH) {\n"
        "    // Can't fit all — favor the farthest\n"
        "    dest = window.scrollY + maxY - vpH + margin;\n"
        "  }\n"
        "  if (dest <= _maxScrollTarget + 5) return;\n"
        "  _maxScrollTarget = dest;\n"
        "  window.scrollTo({top: Math.max(0, dest), behavior: 'smooth'});\n"
        "}\n"
        "function resetScroll() { _maxScrollTarget = 0; window.scrollTo(0,0); }\n"
        "</script>\n"
        "</head>\n<body>\n");

    for (const auto& svgStr : m_renderedSvgs) {
        html += QStringLiteral("<div class=\"page\">");
        html += svgStr;
        html += QStringLiteral("</div>\n");
    }

    html += QStringLiteral("</body>\n</html>");
    return html;
}

int VerovioEngine::partCount() const
{
    return static_cast<int>(m_parts.size());
}

PartInfo VerovioEngine::partInfo(int index) const
{
    PartInfo info;
    if (index < 0 || index >= static_cast<int>(m_parts.size())) return info;
    info.name = m_parts[index].name;
    info.shortName = m_parts[index].shortName;
    info.visible = m_parts[index].visible;
    return info;
}

void VerovioEngine::setPartVisible(int index, bool visible)
{
    if (index < 0 || index >= static_cast<int>(m_parts.size())) return;
    if (m_parts[index].visible == visible) return;
    m_parts[index].visible = visible;
    reloadWithVisibleParts();
}

void VerovioEngine::selectParts(const QList<int>& partNumbers)
{
    if (!m_toolkit || m_musicXmlData.isEmpty()) return;

    if (partNumbers.isEmpty()) {
        applyOptions();
        m_toolkit->LoadData(m_musicXmlData.toStdString());
    } else {
        QString filtered = filterMusicXML(m_musicXmlData, partNumbers);
        applyOptions();
        m_toolkit->LoadData(filtered.toStdString());
    }

    // Force MIDI computation so GetMIDIValuesForElement works correctly
    // (matching electron branch: toolkit.renderToMIDI())
    try { m_toolkit->RenderToMIDI(); } catch (...) {}
}

QRectF VerovioEngine::resolveCursorRect(int tick, int& outPageIndex) const
{
    outPageIndex = 0;
    if (!m_toolkit || m_timemap.empty()) return QRectF();

    double qstamp = tick / 480.0;

    int idx = 0;
    for (int i = 0; i < static_cast<int>(m_timemap.size()); ++i) {
        if (m_timemap[i].qstamp <= qstamp)
            idx = i;
        else
            break;
    }

    const auto& entry = m_timemap[idx];
    if (!entry.elementId.isEmpty()) {
        int page = m_toolkit->GetPageWithElement(entry.elementId.toStdString());
        if (page > 0) outPageIndex = page - 1;
    }

    QSizeF pg = pageSize(outPageIndex);
    double sp = spatium();
    double w = 0.4 * sp;
    double h = pg.height() * 0.3;
    double x = pg.width() * 0.1;
    double y = pg.height() * 0.1;

    return QRectF(x, y, w, h);
}

int VerovioEngine::measureCount() const
{
    return m_measCount;
}

double VerovioEngine::spatium() const
{
    return m_spatiumIn * internalDPI();
}

void VerovioEngine::parseParts(const QString& musicXml)
{
    m_parts.clear();

    QDomDocument doc;
    if (!doc.setContent(musicXml)) return;

    QDomElement root = doc.documentElement();
    QDomElement partList = root.firstChildElement("part-list");
    if (partList.isNull()) return;

    QDomElement sp = partList.firstChildElement("score-part");
    while (!sp.isNull()) {
        InternalPart part;
        part.id = sp.attribute("id");
        part.name = sp.firstChildElement("part-name").text();
        part.shortName = sp.firstChildElement("part-abbreviation").text();
        part.visible = true;
        m_parts.push_back(part);
        sp = sp.nextSiblingElement("score-part");
    }

    m_measCount = 0;
    QDomElement partEl = root.firstChildElement("part");
    if (!partEl.isNull()) {
        QDomElement meas = partEl.firstChildElement("measure");
        while (!meas.isNull()) {
            ++m_measCount;
            meas = meas.nextSiblingElement("measure");
        }
    }
}

QString VerovioEngine::filterMusicXML(const QString& xml, const QList<int>& partNumbers) const
{
    // Matching electron branch: reorder parts to requested order + add bracket group
    QDomDocument doc;
    if (!doc.setContent(xml)) return xml;

    QDomElement root = doc.documentElement();
    QDomElement partList = root.firstChildElement("part-list");

    // Collect all <score-part> elements in original order
    QList<QDomElement> scoreParts;
    QDomElement sp = partList.firstChildElement("score-part");
    while (!sp.isNull()) {
        scoreParts.append(sp);
        sp = sp.nextSiblingElement("score-part");
    }

    // Build ordered list of IDs to keep (preserving requested order)
    QStringList orderedIds;
    for (int pn : partNumbers) {
        int idx = pn - 1; // 1-based to 0-based
        if (idx >= 0 && idx < scoreParts.size())
            orderedIds.append(scoreParts[idx].attribute("id"));
    }
    QSet<QString> keepIds(orderedIds.begin(), orderedIds.end());
    if (keepIds.isEmpty()) return xml;

    // Clear part-list entirely and rebuild in requested order
    while (partList.hasChildNodes())
        partList.removeChild(partList.firstChild());

    // Add bracket group if multiple parts
    if (orderedIds.size() > 1) {
        QDomElement startGroup = doc.createElement("part-group");
        startGroup.setAttribute("type", "start");
        startGroup.setAttribute("number", "1");
        QDomElement groupSymbol = doc.createElement("group-symbol");
        groupSymbol.appendChild(doc.createTextNode("bracket"));
        startGroup.appendChild(groupSymbol);
        QDomElement groupBarline = doc.createElement("group-barline");
        groupBarline.appendChild(doc.createTextNode("yes"));
        startGroup.appendChild(groupBarline);
        partList.appendChild(startGroup);
    }

    // Re-add score-parts in requested order
    for (const QString& id : orderedIds) {
        for (const auto& origSp : scoreParts) {
            if (origSp.attribute("id") == id) {
                partList.appendChild(origSp);
                break;
            }
        }
    }

    if (orderedIds.size() > 1) {
        QDomElement stopGroup = doc.createElement("part-group");
        stopGroup.setAttribute("type", "stop");
        stopGroup.setAttribute("number", "1");
        partList.appendChild(stopGroup);
    }

    // Remove unwanted <part> elements, then re-append kept ones in requested order
    QList<QDomElement> keptPartEls;
    QDomElement partEl = root.firstChildElement("part");
    while (!partEl.isNull()) {
        QDomElement next = partEl.nextSiblingElement("part");
        QString id = partEl.attribute("id");
        if (keepIds.contains(id))
            keptPartEls.append(partEl);
        root.removeChild(partEl);
        partEl = next;
    }
    // Re-append in requested order
    for (const QString& id : orderedIds) {
        for (const auto& pe : keptPartEls) {
            if (pe.attribute("id") == id) {
                root.appendChild(pe);
                break;
            }
        }
    }

    return doc.toString(-1);
}

void VerovioEngine::reloadWithVisibleParts()
{
    if (!m_toolkit || m_musicXmlData.isEmpty()) return;

    // Build ordered list of 1-based part numbers that are visible
    QList<int> visibleParts;
    for (int i = 0; i < static_cast<int>(m_parts.size()); ++i) {
        if (m_parts[i].visible)
            visibleParts.append(i + 1);
    }

    bool allVisible = (visibleParts.size() == static_cast<int>(m_parts.size()));
    QString xmlToLoad = allVisible ? m_musicXmlData
                                   : filterMusicXML(m_musicXmlData, visibleParts);

    applyOptions();
    m_toolkit->LoadData(xmlToLoad.toStdString());
}

void VerovioEngine::buildTimemap()
{
    m_timemap.clear();
    if (!m_toolkit) return;

    // RenderToTimemap can fail if the score has issues — don't block on it
    std::string tmJson;
    try {
        tmJson = m_toolkit->RenderToTimemap();
    } catch (...) {
        return;
    }
    if (tmJson.empty()) return;

    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(tmJson));
    if (!doc.isArray()) return;

    QJsonArray arr = doc.array();
    for (const auto& val : arr) {
        QJsonObject obj = val.toObject();
        TimemapEntry entry;
        entry.qstamp = obj.value("qstamp").toDouble();

        QJsonArray onArr = obj.value("on").toArray();
        if (!onArr.isEmpty())
            entry.elementId = onArr.first().toString();

        m_timemap.push_back(entry);
    }

    std::sort(m_timemap.begin(), m_timemap.end(),
              [](const TimemapEntry& a, const TimemapEntry& b) {
                  return a.qstamp < b.qstamp;
              });
}

std::vector<VerovioEngine::NoteInfo> VerovioEngine::getNotesForPart(
    int partIndex, const QStringList& renderedSvgs) const
{
    std::vector<NoteInfo> notes;
    if (!m_toolkit) return notes;

    int staffN = partIndex + 1; // 1-based

    // Use pre-rendered SVGs to avoid ID regeneration
    // (each RenderToSVG call generates new random IDs)
    for (const auto& svgStr : renderedSvgs) {
        QDomDocument doc;
        if (!doc.setContent(svgStr)) continue;

        // Find all elements with class="note" that have an id
        // Within each <g class="measure">, staves appear in order
        QDomNodeList measures = doc.elementsByTagName("g");
        for (int mi = 0; mi < measures.count(); ++mi) {
            QDomElement g = measures.at(mi).toElement();
            if (g.attribute("class") != "measure") continue;

            // Collect staff groups within this measure
            QList<QDomElement> staves;
            QDomNode child = g.firstChild();
            while (!child.isNull()) {
                if (child.isElement() && child.toElement().attribute("class") == "staff")
                    staves.append(child.toElement());
                child = child.nextSibling();
            }

            QDomElement targetStaff;
            if (staffN <= staves.size())
                targetStaff = staves[staffN - 1];
            else if (!staves.isEmpty())
                targetStaff = staves[0];
            else
                continue;

            // Find note elements within this staff
            QDomNodeList allG = targetStaff.elementsByTagName("g");
            for (int ni = 0; ni < allG.count(); ++ni) {
                QDomElement noteEl = allG.at(ni).toElement();
                if (noteEl.attribute("class") != "note") continue;
                QString id = noteEl.attribute("id");
                if (id.isEmpty()) continue;

                // Get MIDI values from Verovio
                NoteInfo info;
                info.elementId = id;
                info.pitch = 60; // default, try to get from Verovio

                try {
                    std::string jsonStr = m_toolkit->GetMIDIValuesForElement(id.toStdString());
                    QJsonDocument jdoc = QJsonDocument::fromJson(
                        QByteArray::fromStdString(jsonStr));
                    if (jdoc.isObject()) {
                        QJsonObject obj = jdoc.object();
                        if (obj.contains("pitch"))
                            info.pitch = obj.value("pitch").toInt(60);
                    }
                } catch (...) {}

                notes.push_back(info);
            }
        }
    }

    return notes;
}

} // namespace scoretracker
