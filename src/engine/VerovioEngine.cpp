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

    // Use direct override if set, otherwise compute from inches
    int pageW = (m_pageWidthOverride > 0) ? m_pageWidthOverride
                                          : static_cast<int>(m_pageWidthIn * dpi);
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

    // Force MIDI computation AFTER rendering SVGs so IDs match
    try { m_toolkit->RenderToMIDI(); } catch (...) {}

    // Build cursor timemap from SVG notes (RenderToTimemap IDs don't match SVG IDs).
    m_timemap.clear();
    for (const auto& svgStr : m_renderedSvgs) {
        QDomDocument svgDoc;
        if (!svgDoc.setContent(svgStr)) continue;
        QDomNodeList gs = svgDoc.elementsByTagName("g");
        for (int gi = 0; gi < gs.count(); ++gi) {
            QDomElement g = gs.at(gi).toElement();
            if (g.attribute("class") != "note") continue;
            QString id = g.attribute("id");
            if (id.isEmpty()) continue;
            try {
                std::string jsonStr = m_toolkit->GetMIDIValuesForElement(id.toStdString());
                QJsonDocument jdoc = QJsonDocument::fromJson(QByteArray::fromStdString(jsonStr));
                if (jdoc.isObject()) {
                    int midiTime = jdoc.object().value("time").toInt(-1);
                    if (midiTime >= 0) {
                        TimemapEntry entry;
                        entry.qstamp = midiTime / 500.0; // MIDI ticks to quarter notes (500 ticks/quarter in Verovio)
                        entry.elementId = id;
                        m_timemap.push_back(entry);
                    }
                }
            } catch (...) {}
        }
    }
    std::sort(m_timemap.begin(), m_timemap.end(),
              [](const TimemapEntry& a, const TimemapEntry& b) { return a.qstamp < b.qstamp; });
    qDebug() << "renderAllPagesHtml: pages=" << n << "cursor timemap=" << m_timemap.size() << "entries";

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
        "    style:'fill:none;stroke:none;stroke-width:0;opacity:0'},\n"
        "  'highlight-v1': {rc:'hl-v1',\n"
        "    style:'fill:none;stroke:none;stroke-width:0;opacity:0'}\n"
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
        "// Auto-scroll: voice 0 only, forward only.\n"
        "// Ensure the current system AND the next system are both visible.\n"
        "function autoScroll() {\n"
        "  var hl = document.querySelector('.hl-v0');\n"
        "  if (!hl) return;\n"
        "  var note = hl.nextElementSibling;\n"
        "  var sys = note ? (note.closest('.system')||note.closest('.measure')) : null;\n"
        "  if (!sys) return;\n"
        "  // Find the next sibling system\n"
        "  var nextSys = sys.nextElementSibling;\n"
        "  while (nextSys && !(nextSys.classList && nextSys.classList.contains('system')))\n"
        "    nextSys = nextSys.nextElementSibling;\n"
        "  // Bottom edge we need visible: next system's bottom, or current if no next\n"
        "  var rects = sys.querySelectorAll('.hl-v0,.hl-v1');\n"
        "  rects.forEach(function(r){r.style.display='none';});\n"
        "  var sr = sys.getBoundingClientRect();\n"
        "  var needBot = sr.bottom;\n"
        "  if (nextSys) {\n"
        "    var nr = nextSys.getBoundingClientRect();\n"
        "    needBot = nr.bottom;\n"
        "  }\n"
        "  rects.forEach(function(r){r.style.display='';});\n"
        "  var vpH = window.innerHeight;\n"
        "  // Both systems fully visible? Do nothing.\n"
        "  if (sr.top >= 0 && needBot <= vpH) return;\n"
        "  // Scroll current system top to 10px\n"
        "  window.scrollTo({top: Math.max(0, window.scrollY+sr.top-10), behavior:'smooth'});\n"
        "}\n"
        "function resetScroll() { window.scrollTo(0,0); }\n"
        "// Playback cursor: blue vertical line spanning the system.\n"
        "// Live DOM queries for element positions (simple, always correct).\n"
        "var _cursorEl = null;\n"
        "var _timemap = []; // [{tick, id}, ...] sorted by tick\n"
        "function setTimemap(tm) { _timemap = tm; }\n"
        "// Pre-cache all system heights at page load (clean, no highlights).\n"
        "var _sysHeights = new Map();\n"
        "setTimeout(function() {\n"
        "  document.querySelectorAll('.system').forEach(function(sys) {\n"
        "    _sysHeights.set(sys, sys.getBoundingClientRect().height);\n"
        "  });\n"
        "}, 300);\n"
        "function _getSysBounds(sys) {\n"
        "  var sr = sys.getBoundingClientRect();\n"
        "  var h = _sysHeights.get(sys) || sr.height;\n"
        "  return {top: sr.top + window.scrollY, height: h};\n"
        "}\n"
        "function setCursorTick(tick) {\n"
        "  if (!_timemap.length) return;\n"
        "  if (!_cursorEl) {\n"
        "    _cursorEl = document.createElement('div');\n"
        "    _cursorEl.style.cssText = 'position:absolute;background:rgba(50,100,255,0.35);pointer-events:none;z-index:999;';\n"
        "    document.body.appendChild(_cursorEl);\n"
        "  }\n"
        "  var lo = 0, hi = _timemap.length - 1;\n"
        "  while (lo < hi - 1) { var m = (lo+hi)>>1; if (_timemap[m].tick <= tick) lo = m; else hi = m; }\n"
        "  var e1 = document.getElementById(_timemap[lo].id);\n"
        "  if (!e1) { _cursorEl.style.display='none'; return; }\n"
        "  var s1 = e1.closest('.system');\n"
        "  var r1 = e1.getBoundingClientRect();\n"
        "  var x = r1.left + r1.width/2;\n"
        "  if (lo !== hi) {\n"
        "    var e2 = document.getElementById(_timemap[hi].id);\n"
        "    if (e2) {\n"
        "      var s2 = e2.closest('.system');\n"
        "      var dt = _timemap[hi].tick - _timemap[lo].tick;\n"
        "      if (s1 && s2 && s1 === s2 && dt > 0) {\n"
        "        // Same system: interpolate between note positions\n"
        "        var r2 = e2.getBoundingClientRect();\n"
        "        var f = Math.max(0, Math.min(1, (tick - _timemap[lo].tick) / dt));\n"
        "        x += (r2.left + r2.width/2 - x) * f;\n"
        "      } else if (s1 && s2 && s1 !== s2 && dt > 0) {\n"
        "        var f = Math.max(0, Math.min(1, (tick - _timemap[lo].tick) / dt));\n"
        "        if (f < 0.5) {\n"
        "          // First half: still in old system, interpolate toward right edge\n"
        "          var rightEdge = s1.getBoundingClientRect().right - 10;\n"
        "          x += (rightEdge - x) * (f * 2);\n"
        "        } else {\n"
        "          // Second half: in new system, start at first note position (skip clef/key)\n"
        "          var r2 = e2.getBoundingClientRect();\n"
        "          s1 = s2; // switch cursor to new system\n"
        "          x = r2.left + r2.width/2;\n"
        "        }\n"
        "      }\n"
        "    }\n"
        "  }\n"
        "  if (!s1) { _cursorEl.style.display='none'; return; }\n"
        "  var sb = _getSysBounds(s1);\n"
        "  _cursorEl.style.left = (x - 2 + window.scrollX) + 'px';\n"
        "  _cursorEl.style.top = sb.top + 'px';\n"
        "  _cursorEl.style.width = '4px';\n"
        "  _cursorEl.style.height = sb.height + 'px';\n"
        "  _cursorEl.style.display = 'block';\n"
        "}\n"
        "function hideCursor() { if (_cursorEl) _cursorEl.style.display='none'; }\n"
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

    m_selectedParts = partNumbers;

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

        // Only store entries with a valid element ID (skip rests)
        if (!entry.elementId.isEmpty())
            m_timemap.push_back(entry);
    }

    std::sort(m_timemap.begin(), m_timemap.end(),
              [](const TimemapEntry& a, const TimemapEntry& b) {
                  return a.qstamp < b.qstamp;
              });
}

QString VerovioEngine::timemapAsJs() const
{
    qDebug() << "timemapAsJs: m_timemap has" << m_timemap.size() << "entries";
    if (m_timemap.empty()) return QStringLiteral("setTimemap([]);");

    QString js = "setTimemap([";
    for (size_t i = 0; i < m_timemap.size(); ++i) {
        if (i > 0) js += ",";
        int tick = static_cast<int>(m_timemap[i].qstamp * 480.0);
        js += QStringLiteral("{tick:%1,id:'%2'}").arg(tick).arg(m_timemap[i].elementId);
    }
    js += "]);";

    // Debug: show first 3 entries
    for (size_t i = 0; i < std::min(m_timemap.size(), size_t(3)); ++i) {
        qDebug() << "  timemap[" << i << "] qstamp=" << m_timemap[i].qstamp
                 << "tick=" << int(m_timemap[i].qstamp * 480)
                 << "id=" << m_timemap[i].elementId;
    }
    return js;
}

QString VerovioEngine::elementIdAtTick(int tick) const
{
    if (m_timemap.empty()) return QString();
    double qstamp = tick / 480.0;
    int idx = 0;
    for (int i = 0; i < static_cast<int>(m_timemap.size()); ++i) {
        if (m_timemap[i].qstamp <= qstamp) idx = i;
        else break;
    }
    return m_timemap[idx].elementId;
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

                NoteInfo info;
                info.elementId = id;

                try {
                    std::string jsonStr = m_toolkit->GetMIDIValuesForElement(id.toStdString());
                    QJsonDocument jdoc = QJsonDocument::fromJson(
                        QByteArray::fromStdString(jsonStr));
                    if (jdoc.isObject()) {
                        QJsonObject obj = jdoc.object();
                        info.pitch = obj.value("pitch").toInt(60);
                        info.midiTime = obj.value("time").toInt(-1);
                        info.midiDur = obj.value("duration").toInt(0);
                    }
                } catch (...) {}

                notes.push_back(info);
            }
        }
    }

    // Detect tied-back notes: build a set of MIDI times that have <tie type="stop"/>
    // from the original MusicXML. Match to SVG notes by midiTime + pitch.
    {
        // Find the original part ID for this filtered partIndex
        QDomDocument xmlDoc;
        xmlDoc.setContent(m_musicXmlData);
        QDomElement xmlRoot = xmlDoc.documentElement();

        QList<QString> allPartIds;
        QDomElement xmlSp = xmlRoot.firstChildElement("part-list").firstChildElement("score-part");
        while (!xmlSp.isNull()) {
            allPartIds.append(xmlSp.attribute("id"));
            xmlSp = xmlSp.nextSiblingElement("score-part");
        }

        // Map filtered partIndex to original part using m_selectedParts
        int origIdx = -1;
        if (!m_selectedParts.isEmpty() && partIndex < m_selectedParts.size()) {
            origIdx = m_selectedParts[partIndex] - 1; // 1-based to 0-based
        } else {
            origIdx = partIndex; // no filtering, direct mapping
        }


        if (origIdx >= 0 && origIdx < allPartIds.size()) {
            // Find the <part> element
            QDomElement xmlPart = xmlRoot.firstChildElement("part");
            while (!xmlPart.isNull() && xmlPart.attribute("id") != allPartIds[origIdx])
                xmlPart = xmlPart.nextSiblingElement("part");

            if (!xmlPart.isNull()) {
                // Get <divisions> (ticks per quarter note in MusicXML)
                int divisions = 1;
                // Walk measures, accumulate time, collect tied-stop (time, pitch) pairs
                struct TieStop { int midiTime; int pitch; };
                std::vector<TieStop> tieStops;
                int currentTime = 0; // in MIDI ticks (will convert from divisions)
                // MusicXML divisions → MIDI ticks conversion: midi_ticks = xml_duration * 500 / divisions
                // (Verovio uses 500 ticks per quarter by default)

                QDomElement meas = xmlPart.firstChildElement("measure");
                while (!meas.isNull()) {
                    QDomElement el = meas.firstChildElement();
                    while (!el.isNull()) {
                        if (el.tagName() == "attributes") {
                            QDomElement divEl = el.firstChildElement("divisions");
                            if (!divEl.isNull()) divisions = divEl.text().toInt();
                        }
                        if (el.tagName() == "forward") {
                            int dur = el.firstChildElement("duration").text().toInt();
                            currentTime += dur * 500 / divisions;
                        }
                        if (el.tagName() == "backup") {
                            int dur = el.firstChildElement("duration").text().toInt();
                            currentTime -= dur * 500 / divisions;
                        }
                        if (el.tagName() == "note") {
                            bool isRest = !el.firstChildElement("rest").isNull();
                            bool isChord = !el.firstChildElement("chord").isNull();
                            int dur = el.firstChildElement("duration").text().toInt();
                            int midiTicks = dur * 500 / divisions;

                            if (!isRest) {
                                // Get pitch
                                QDomElement pitchEl = el.firstChildElement("pitch");
                                int step = 0, octave = 0, alter = 0;
                                if (!pitchEl.isNull()) {
                                    QString s = pitchEl.firstChildElement("step").text();
                                    octave = pitchEl.firstChildElement("octave").text().toInt();
                                    alter = pitchEl.firstChildElement("alter").text().toInt();
                                    static const int steps[] = {9,11,0,2,4,5,7}; // A=9,B=11,C=0,...
                                    if (s.size() == 1) step = steps[s[0].unicode() - 'A'];
                                }
                                int midiPitch = (octave + 1) * 12 + step + alter;

                                // Check for <tie type="stop"/>
                                bool hasTieStop = false;
                                QDomElement tie = el.firstChildElement("tie");
                                while (!tie.isNull()) {
                                    if (tie.attribute("type") == "stop")
                                        hasTieStop = true;
                                    tie = tie.nextSiblingElement("tie");
                                }
                                if (hasTieStop)
                                    tieStops.push_back({currentTime, midiPitch});
                            }
                            if (!isChord) currentTime += midiTicks;
                        }
                        el = el.nextSiblingElement();
                    }
                    meas = meas.nextSiblingElement("measure");
                }

                // Match tieStops to SVG notes by (midiTime, pitch)
                int matched = 0;
                for (auto& note : notes) {
                    for (auto& ts : tieStops) {
                        if (note.midiTime == ts.midiTime && note.pitch == ts.pitch) {
                            note.tiedBack = true;
                            matched++;
                            break;
                        }
                    }
                }
            }
        }
    }

    return notes;
}

} // namespace scoretracker
