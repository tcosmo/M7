#include <QApplication>
#include <QDebug>
#include <QFileInfo>
#include <QPalette>
#include <QStyleFactory>

#include "theme.h"
#include "modularity/ioc.h"
#include "iapplication.h"
#include "io/ifilesystem.h"
#include "io/internal/filesystem.h"
#include "iglobalconfiguration.h"
#include "internal/globalconfiguration.h"
#include "iinteractive.h"
#include "internal/interactive.h"
#include "icryptographichash.h"
#include "internal/cryptographichash.h"
#include "iprocess.h"
#include "internal/process.h"
#include "isysteminfo.h"
#include "internal/systeminfo.h"
#include "itickerprovider.h"
#include "internal/tickerprovider.h"
#include "settings.h"

#include "draw/drawmodule.h"
#include "engraving/engravingmodule.h"
#include "importexport/musicxml/imusicxmlconfiguration.h"

#include "app.h"

// Minimal IMusicXmlConfiguration stub
class MusicXmlConfigStub : public mu::iex::musicxml::IMusicXmlConfiguration
{
public:
    bool importBreaks() const override { return true; }
    void setImportBreaks(bool) override {}
    muse::async::Channel<bool> importBreaksChanged() const override { return {}; }

    bool importLayout() const override { return true; }
    void setImportLayout(bool) override {}
    muse::async::Channel<bool> importLayoutChanged() const override { return {}; }

    bool exportLayout() const override { return true; }
    void setExportLayout(bool) override {}

    bool exportMu3Compat() const override { return false; }
    void setExportMu3Compat(bool) override {}

    MusicXmlExportBreaksType exportBreaksType() const override { return MusicXmlExportBreaksType::All; }
    void setExportBreaksType(MusicXmlExportBreaksType) override {}

    bool exportInvisibleElements() const override { return true; }
    void setExportInvisibleElements(bool) override {}

    bool needUseDefaultFont() const override { return false; }
    void setNeedUseDefaultFont(bool) override {}
    muse::async::Channel<bool> needUseDefaultFontChanged() const override { return {}; }
    void setNeedUseDefaultFontOverride(std::optional<bool>) override {}

    bool needAskAboutApplyingNewStyle() const override { return false; }
    void setNeedAskAboutApplyingNewStyle(bool) override {}
    muse::async::Channel<bool> needAskAboutApplyingNewStyleChanged() const override { return {}; }

    bool inferTextType() const override { return true; }
    void setInferTextType(bool) override {}
    muse::async::Channel<bool> inferTextTypeChanged() const override { return {}; }
    void setInferTextTypeOverride(std::optional<bool>) override {}
};

// Minimal IApplication implementation for standalone use
class StandaloneApp : public muse::IApplication
{
public:
    StandaloneApp()
        : m_ctx(std::make_shared<muse::modularity::Context>()) {}

    muse::String name() const override { return muse::String(u"jamjammin"); }
    muse::String title() const override { return muse::String(u"JamJammin'"); }
    bool unstable() const override { return false; }
    muse::Version version() const override { return muse::Version(0, 1, 0); }
    muse::Version fullVersion() const override { return version(); }
    muse::String build() const override { return muse::String(u"1"); }
    muse::String revision() const override { return muse::String(); }
    RunMode runMode() const override { return RunMode::GuiApp; }
    bool noGui() const override { return false; }
    void restart() override {}
    void perform() override {}
    void finish() override {}

    const muse::modularity::ContextPtr iocContext() const override { return m_ctx; }
    muse::modularity::ModulesIoC* ioc() const override { return muse::modularity::globalIoc(); }

    void processEvents() override { QApplication::processEvents(); }

#ifndef NO_QT_SUPPORT
    QWindow* focusWindow() const override { return nullptr; }
    bool notify(QObject*, QEvent*) override { return false; }
    Qt::KeyboardModifiers keyboardModifiers() const override { return Qt::NoModifier; }
#endif

private:
    muse::modularity::ContextPtr m_ctx;
};

int main(int argc, char* argv[])
{
    QApplication qapp(argc, argv);
    qapp.setApplicationName("JamJammin");
    qapp.setOrganizationName("MMMM");

    // Force dark Discord-style palette — no light mode
    qapp.setStyle(QStyleFactory::create("Fusion"));
    QPalette darkPal;
    darkPal.setColor(QPalette::Window,          scoretracker::Theme::surfaceBg());
    darkPal.setColor(QPalette::WindowText,      scoretracker::Theme::textPrimary());
    darkPal.setColor(QPalette::Base,            scoretracker::Theme::inputBg());
    darkPal.setColor(QPalette::AlternateBase,   scoretracker::Theme::panelBg());
    darkPal.setColor(QPalette::Text,            scoretracker::Theme::textPrimary());
    darkPal.setColor(QPalette::Button,          scoretracker::Theme::panelBg());
    darkPal.setColor(QPalette::ButtonText,      scoretracker::Theme::textPrimary());
    darkPal.setColor(QPalette::BrightText,      Qt::white);
    darkPal.setColor(QPalette::Highlight,       scoretracker::Theme::accent());
    darkPal.setColor(QPalette::HighlightedText, Qt::white);
    darkPal.setColor(QPalette::Disabled, QPalette::Text,       scoretracker::Theme::textDisabled());
    darkPal.setColor(QPalette::Disabled, QPalette::ButtonText, scoretracker::Theme::textDisabled());
    darkPal.setColor(QPalette::Disabled, QPalette::WindowText, scoretracker::Theme::textDisabled());
    qapp.setPalette(darkPal);
    qapp.setStyleSheet(scoretracker::Theme::globalStyleSheet());

    // Parse arguments
    QString musicXmlPath;
    QString audioPath;
    QString youtubeUrl;
    QString sourcesPath;
    QString beatDataPath;
    QList<int> visibleParts;
    bool startSyncMode = false;
    bool startPlayMode = false;
    bool preferFile = false;

    QStringList args = qapp.arguments();
    for (int i = 1; i < args.size(); ++i) {
        if (args[i] == "--sync") {
            startSyncMode = true;
        } else if (args[i] == "--play") {
            startPlayMode = true;
        } else if (args[i] == "--file") {
            preferFile = true;
        } else if (args[i] == "--parts" && i + 1 < args.size()) {
            for (const QString& s : args[++i].split(',')) {
                bool ok;
                int n = s.trimmed().toInt(&ok);
                if (ok && n > 0) visibleParts.append(n);
            }
        } else if (args[i] == "--beats" && i + 1 < args.size()) {
            beatDataPath = args[++i];
        } else if (args[i] == "--youtube" && i + 1 < args.size()) {
            youtubeUrl = args[++i];
        } else if (args[i] == "--sources" && i + 1 < args.size()) {
            sourcesPath = args[++i];
        } else if (musicXmlPath.isEmpty()) {
            musicXmlPath = args[i];
        } else if (audioPath.isEmpty()) {
            audioPath = args[i];
        }
    }

    if (musicXmlPath.isEmpty()) {
        qWarning() << "Usage: scoretracker <musicxml-file> [audio-file] [--youtube url] [--sources path] [--beats beatdata.json] [--parts 1,4,5] [--sync]";
        qWarning() << "  musicxml-file: Path to MusicXML score (.musicxml, .xml, .mxl)";
        qWarning() << "  audio-file:    Path to audio recording (.m4a, .mp3, .wav)";
        qWarning() << "  --youtube:     YouTube video URL (mutually exclusive with audio-file)";
        qWarning() << "  --sources:     Path to sources.json (overrides --youtube)";
        qWarning() << "  --beats:       Path to beat data JSON file";
        qWarning() << "  --parts:       Comma-separated 1-based part numbers to show";
        qWarning() << "  --sync:        Start in sync mode";
        qWarning() << "  --play:        Start in play mode";
        qWarning() << "  --file:        Prefer file source over YouTube";
        return 1;
    }

    // Set up IoC manually (since we skip GlobalModule)
    auto* ioc = muse::modularity::globalIoc();

    auto application = std::make_shared<StandaloneApp>();
    auto iocCtx = application->iocContext();
    ioc->registerExport<muse::IApplication>("app", application);
    ioc->registerExport<muse::io::IFileSystem>("app", new muse::io::FileSystem());
    ioc->registerExport<muse::IGlobalConfiguration>("app", std::make_shared<muse::GlobalConfiguration>(iocCtx));
    ioc->registerExport<muse::IInteractive>("app", std::make_shared<muse::Interactive>(iocCtx));
    ioc->registerExport<muse::ICryptographicHash>("app", new muse::CryptographicHash());
    ioc->registerExport<muse::IProcess>("app", new muse::Process());
    ioc->registerExport<muse::ISystemInfo>("app", std::make_shared<muse::SystemInfo>());
    ioc->registerExport<muse::ITickerProvider>("app", std::make_shared<muse::TickerProvider>());
    ioc->registerExport<mu::iex::musicxml::IMusicXmlConfiguration>("app", std::make_shared<MusicXmlConfigStub>());

    // Load settings (normally done by GlobalModule::onPreInit)
    muse::settings()->load();

    // Initialize MuseScore modules
    muse::draw::DrawModule drawModule;
    mu::engraving::EngravingModule engravingModule;

    // 1. Register exports
    drawModule.registerExports();
    engravingModule.registerExports();

    // 2. Resolve imports
    drawModule.resolveImports();
    engravingModule.resolveImports();

    // 3. Register resources (Q_INIT_RESOURCE for fonts)
    engravingModule.registerResources();

    // 4. Register UI types
    engravingModule.registerUiTypes();

    // 5. Init
    auto runMode = muse::IApplication::RunMode::GuiApp;
    drawModule.onInit(runMode);
    engravingModule.onInit(runMode);

    qDebug() << "MuseScore modules initialized successfully";

    // Create and show the app
    scoretracker::App app;

    if (!app.loadScore(musicXmlPath)) {
        qWarning() << "Failed to load score:" << musicXmlPath;
        return 1;
    }

    if (!beatDataPath.isEmpty()) {
        if (!app.loadBeatData(beatDataPath)) {
            qWarning() << "Failed to load beat data:" << beatDataPath;
        }
    }

    if (!visibleParts.isEmpty()) {
        app.setVisibleParts(visibleParts);
    }

    if (!sourcesPath.isEmpty()) {
        app.loadSources(sourcesPath);
    } else if (!youtubeUrl.isEmpty()) {
        app.loadYouTube(youtubeUrl);
    } else if (!audioPath.isEmpty()) {
        if (!app.loadAudio(audioPath)) {
            qWarning() << "Failed to load audio:" << audioPath;
            // Continue without audio
        }
    }

    if (preferFile) {
        app.selectFileSource();
    }

    app.show();

    if (startSyncMode) {
        app.startSyncMode();
    } else if (startPlayMode) {
        app.startPlayMode();
    }

    int ret = qapp.exec();

    // Cleanup
    engravingModule.onDeinit();
    engravingModule.onDestroy();

    return ret;
}
