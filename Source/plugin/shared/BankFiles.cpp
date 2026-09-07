#include "plugin/shared/BankFiles.h"

#include "plugin/shared/BankJson.h"

#include <memory>
#include <utility>

namespace chipboy::plugin {

using namespace juce;

namespace {

constexpr const char* kBankExtension = ".chipboy";
constexpr const char* kSongExtension = ".chipboysong";
constexpr int kOpenFlags = FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles;
constexpr int kSaveFlags = FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting;

/// The one chooser at a time; it lives until its callback has run. Opening
/// another while one is up dismisses the first.
std::unique_ptr<FileChooser>& chooser()
{
    static std::unique_ptr<FileChooser> c;
    return c;
}

String legalName(const String& suggested, const char* fallback)
{
    const String n = File::createLegalFileName(suggested.trim());
    return n.isEmpty() ? String(fallback) : n;
}

/// The text is serialised before the dialog opens: the caller's object need
/// not outlive the call.
void saveText(Component* parent, const String& title, const String& extension, const String& name, String text,
              std::function<void(bool, File)> done)
{
    chooser() = std::make_unique<FileChooser>(title, banksFolder().getChildFile(name + extension), "*" + extension, true, false, parent);
    chooser()->launchAsync(kSaveFlags, [extension, text = std::move(text), done = std::move(done)](const FileChooser& fc) {
        File file = fc.getResult();   // copied first: done may open another chooser
        if (file == File()) { if (done) done(false, {}); return; }
        if (!file.hasFileExtension(extension)) file = file.withFileExtension(extension);
        const bool ok = file.replaceWithText(text);
        if (done) done(ok, file);
    });
}

/// `done` gets an empty string on cancel or an unreadable file.
void loadText(Component* parent, const String& title, const String& extension, std::function<void(String, File)> done)
{
    chooser() = std::make_unique<FileChooser>(title, banksFolder(), "*" + extension, true, false, parent);
    chooser()->launchAsync(kOpenFlags, [done = std::move(done)](const FileChooser& fc) {
        const File file = fc.getResult();
        const String text = file.existsAsFile() ? file.loadFileAsString() : String();
        if (done) done(text, file);
    });
}

} // namespace

File banksFolder()
{
    const File folder = File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("ChipBoy").getChildFile("Banks");
    folder.createDirectory();
    return folder;
}

void saveBankAs(Component* parent, const bank::Bank& b, const String& suggestedName, std::function<void(bool, File)> done)
{
    saveText(parent, "Save bank", kBankExtension, legalName(suggestedName, "Bank"), bankToJson(b), std::move(done));
}

void loadBank(Component* parent, std::function<void(std::unique_ptr<bank::Bank>, File)> done)
{
    loadText(parent, "Open bank", kBankExtension, [done = std::move(done)](const String& text, const File& file) {
        std::unique_ptr<bank::Bank> loaded;
        if (text.isNotEmpty()) {
            loaded = std::make_unique<bank::Bank>();
            if (!bankFromJson(text, *loaded)) loaded.reset();
        }
        if (done) done(std::move(loaded), file);
    });
}

void saveSongAs(Component* parent, const tracker::Song& s, const String& suggestedName, std::function<void(bool, File)> done)
{
    saveText(parent, "Save song", kSongExtension, legalName(suggestedName, "Song"), songToJson(s), std::move(done));
}

void loadSong(Component* parent, std::function<void(std::unique_ptr<tracker::Song>, File)> done)
{
    loadText(parent, "Open song", kSongExtension, [done = std::move(done)](const String& text, const File& file) {
        std::unique_ptr<tracker::Song> loaded;
        if (text.isNotEmpty()) {
            loaded = std::make_unique<tracker::Song>();
            if (!songFromJson(text, *loaded)) loaded.reset();
        }
        if (done) done(std::move(loaded), file);
    });
}

} // namespace chipboy::plugin
