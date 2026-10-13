// First launch without game data: this program holds no part of the game's
// pictures, sounds or text. The player points it at the files of their own copy
// of Crystal Defenders (Xbox 360), which are copied into the "game" folder next
// to the program.
#include "setup.h"

#include <windows.h>

#include <shlobj.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <system_error>
#include <vector>

static bool French()
{
    return PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_FRENCH;
}

static const wchar_t* Text(const wchar_t* french, const wchar_t* english)
{
    return French() ? french : english;
}

// The folder must hold the game's executable and its data.
static bool LooksLikeTheGame(const std::filesystem::path& folder)
{
    char magic[4]{};
    if (FILE* file = _wfopen((folder / "default.xex").c_str(), L"rb"))
    {
        fread(magic, 1, 4, file);
        fclose(file);
    }
    std::error_code error;
    return memcmp(magic, "XEX2", 4) == 0 && std::filesystem::exists(folder / "Media" / "sound.bin", error);
}

// ---- Xbox 360 content package (STFS) ----
// The game as it sits on the console's storage: one file named by a long
// hexadecimal number, in <title id>\000D0000\. It is a small file system: 4 KB
// blocks, with a block of hashes inserted before every 170 data blocks and
// further ones at each higher level.

constexpr uint32_t kTitleId = 0x58410989; // Crystal Defenders

static uint32_t Big32(const uint8_t* p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

static uint32_t Little24(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16);
}

struct Package
{
    FILE* file{};
    uint32_t firstHashTable{}; // file offset of the first hash block
    uint32_t tableShift{};     // 0: one hash block at each position, 1: two

    uint64_t BlockOffset(uint32_t block) const
    {
        uint32_t backing = (((block + 0xAA) / 0xAA) << tableShift) + block;
        if (block >= 0xAA)
        {
            backing += ((block + 0x70E4) / 0x70E4) << tableShift;
            if (block >= 0x70E4)
                backing += 1u << tableShift;
        }
        return (uint64_t(backing) << 12) + firstHashTable;
    }

    bool ReadBlock(uint32_t block, uint8_t* out) const
    {
        return _fseeki64(file, int64_t(BlockOffset(block)), SEEK_SET) == 0 && fread(out, 1, 0x1000, file) == 0x1000;
    }

    // The block following one in a chain, from its entry in the hash block.
    // Only for packages with single hash blocks, where there is no choice of
    // table to make.
    bool NextBlock(uint32_t block, uint32_t& next) const
    {
        if (tableShift != 0)
            return false;
        uint32_t table = block < 0xAA ? 0 : (block / 0xAA) * 0xAB + (block / 0x70E4) + 1 + (block >= 0x70E4 ? 1 : 0);
        uint8_t entry[0x18];
        uint64_t offset = (uint64_t(table) << 12) + firstHashTable + (block % 0xAA) * 0x18;
        if (_fseeki64(file, int64_t(offset), SEEK_SET) != 0 || fread(entry, 1, sizeof(entry), file) != sizeof(entry))
            return false;
        next = (uint32_t(entry[0x15]) << 16) | (uint32_t(entry[0x16]) << 8) | entry[0x17];
        return true;
    }
};

// Is this file a package of the game?
static bool IsGamePackage(const std::filesystem::path& path)
{
    uint8_t header[0x364]{};
    bool ok = false;
    if (FILE* file = _wfopen(path.c_str(), L"rb"))
    {
        ok = fread(header, 1, sizeof(header), file) == sizeof(header);
        fclose(file);
    }
    return ok && (memcmp(header, "LIVE", 4) == 0 || memcmp(header, "PIRS", 4) == 0 || memcmp(header, "CON ", 4) == 0) &&
        Big32(header + 0x360) == kTitleId;
}

// Looks for the package in a folder and the few levels below it: the player may
// point at the game's folder, the title id folder, or the one holding the file.
static std::filesystem::path FindPackage(const std::filesystem::path& folder)
{
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(folder, std::filesystem::directory_options::skip_permission_denied, error);
         !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
    {
        if (it.depth() > 3)
        {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code ignored;
        if (it->is_regular_file(ignored) && it->file_size(ignored) > 0x10000 && IsGamePackage(it->path()))
            return it->path();
    }
    return {};
}

// Writes every file of the package under the target folder.
static bool ExtractPackage(const std::filesystem::path& path, const std::filesystem::path& target)
{
    Package package;
    package.file = _wfopen(path.c_str(), L"rb");
    if (package.file == nullptr)
        return false;

    uint8_t header[0x400]{};
    bool ok = fread(header, 1, sizeof(header), package.file) == sizeof(header);
    package.firstHashTable = (Big32(header + 0x340) + 0xFFF) & 0xFFFFF000;
    package.tableShift = (header[0x37B] & 1) ? 0 : 1;
    uint32_t tableBlocks = uint32_t(header[0x37C]) | (uint32_t(header[0x37D]) << 8);
    uint32_t tableBlock = Little24(header + 0x37E);

    struct Entry
    {
        std::string name;
        bool directory;
        bool consecutive;
        uint32_t blocks, firstBlock, parent, size;
    };
    std::vector<Entry> entries;
    std::vector<uint8_t> block(0x1000);
    for (uint32_t i = 0; ok && i < tableBlocks; i++)
    {
        ok = package.ReadBlock(tableBlock, block.data());
        for (uint32_t e = 0; ok && e < 0x40; e++)
        {
            const uint8_t* raw = block.data() + e * 0x40;
            uint32_t length = raw[0x28] & 0x3F;
            Entry entry;
            entry.name.assign((const char*)raw, length);
            entry.directory = raw[0x28] & 0x80;
            entry.consecutive = raw[0x28] & 0x40;
            entry.blocks = Little24(raw + 0x29);
            entry.firstBlock = Little24(raw + 0x2F);
            entry.parent = (uint32_t(raw[0x32]) << 8) | raw[0x33];
            entry.size = Big32(raw + 0x34);
            entries.push_back(entry); // unused slots are kept: parents are slot numbers
        }
        if (ok && i + 1 < tableBlocks)
            ok = package.NextBlock(tableBlock, tableBlock) || (tableBlock++, package.tableShift != 0);
    }

    for (size_t i = 0; ok && i < entries.size(); i++)
    {
        const Entry& entry = entries[i];
        if (entry.name.empty())
            continue;
        std::filesystem::path relative = entry.name;
        for (uint32_t parent = entry.parent, depth = 0; parent != 0xFFFF && parent < entries.size() && depth < 16; depth++)
        {
            relative = std::filesystem::path(entries[parent].name) / relative;
            parent = entries[parent].parent;
        }
        // Names come from the package: nothing may land outside the target.
        if (relative.is_absolute() || relative.native().find(L"..") != std::wstring::npos)
            continue;

        std::error_code error;
        std::filesystem::path out = target / relative;
        if (entry.directory)
        {
            std::filesystem::create_directories(out, error);
            continue;
        }
        std::filesystem::create_directories(out.parent_path(), error);
        FILE* file = _wfopen(out.c_str(), L"wb");
        ok = file != nullptr;
        uint32_t current = entry.firstBlock;
        for (uint32_t remaining = entry.size; ok && remaining != 0;)
        {
            uint32_t chunk = remaining < 0x1000 ? remaining : 0x1000;
            ok = package.ReadBlock(current, block.data()) && fwrite(block.data(), 1, chunk, file) == chunk;
            remaining -= chunk;
            if (ok && remaining != 0)
            {
                if (entry.consecutive)
                    current++;
                else
                    ok = package.NextBlock(current, current);
            }
        }
        if (file != nullptr)
            fclose(file);
    }
    fclose(package.file);
    return ok;
}

static std::filesystem::path BrowseForFolder()
{
    BROWSEINFOW info{};
    info.lpszTitle = Text(L"Choisissez le dossier de votre copie de Crystal Defenders (Xbox 360) : le dossier « CRYSTAL DEFENDERS » ou « 58410989 » copié depuis la console, ou un dossier contenant default.xex.",
        L"Choose the folder of your copy of Crystal Defenders (Xbox 360): the \"CRYSTAL DEFENDERS\" or \"58410989\" folder copied from the console, or a folder holding default.xex.");
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
    PIDLIST_ABSOLUTE item = SHBrowseForFolderW(&info);
    if (item == nullptr)
        return {};
    wchar_t path[MAX_PATH]{};
    SHGetPathFromIDListW(item, path);
    CoTaskMemFree(item);
    return path;
}

bool RunFirstTimeSetup(const std::filesystem::path& programFolder)
{
    // CD_SETUP_SOURCE=<folder>: take the game from there without asking (tests).
    const wchar_t* preset = _wgetenv(L"CD_SETUP_SOURCE");
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const wchar_t* title = Text(L"Crystal Defenders - installation", L"Crystal Defenders - setup");

    if (preset == nullptr && MessageBoxW(nullptr,
            Text(L"Bienvenue !\n\n"
                 L"Ce programme fait tourner Crystal Defenders (Xbox 360) sur PC, mais il ne contient pas les données du jeu. "
                 L"Il faut votre propre copie du jeu : le dossier « CRYSTAL DEFENDERS » (ou « 58410989 ») tel qu'il se trouve sur le "
                 L"stockage de la console.\n\n"
                 L"Cliquez sur OK pour indiquer ce dossier. Le jeu sera installé à côté du programme, une seule fois.",
                L"Welcome!\n\n"
                L"This program runs Crystal Defenders (Xbox 360) on PC, but it does not contain the game's data. "
                L"It needs your own copy of the game: the \"CRYSTAL DEFENDERS\" (or \"58410989\") folder as found on the console's "
                L"storage.\n\n"
                L"Click OK to choose that folder. The game will be installed next to the program, once."),
            title, MB_OKCANCEL | MB_ICONINFORMATION) != IDOK)
        return false;

    for (;;)
    {
        std::filesystem::path source = preset != nullptr ? std::filesystem::path(preset) : BrowseForFolder();
        if (source.empty())
            return false;
        // Either the console's package, or files someone already extracted.
        std::filesystem::path package = LooksLikeTheGame(source) ? std::filesystem::path() : FindPackage(source);
        if (!LooksLikeTheGame(source) && package.empty())
        {
            if (preset != nullptr)
                return false;
            if (MessageBoxW(nullptr,
                    Text(L"Le jeu n'a pas été trouvé dans ce dossier. Il faut le dossier « CRYSTAL DEFENDERS » ou « 58410989 » de la console, ou un dossier contenant default.xex et Media.\n\nChoisir un autre dossier ?",
                        L"The game was not found in this folder. It takes the console's \"CRYSTAL DEFENDERS\" or \"58410989\" folder, or a folder holding default.xex and Media.\n\nChoose another folder?"),
                    title, MB_RETRYCANCEL | MB_ICONWARNING) != IDRETRY)
                return false;
            continue;
        }

        std::error_code error;
        std::filesystem::path target = programFolder / "game";
        std::filesystem::create_directories(target, error);
        if (!package.empty())
        {
            HCURSOR previous = SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_WAIT));
            if (!ExtractPackage(package, target))
                error = std::make_error_code(std::errc::io_error);
            SetCursor(previous);
        }
        else
        {
            std::filesystem::copy(source, target, std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing, error);
        }
        if (error || !LooksLikeTheGame(target))
        {
            std::wstring message = Text(L"La copie des fichiers a échoué. Vérifiez qu'il reste de la place et que le dossier du programme est accessible en écriture.\n\n",
                L"Copying the files failed. Check there is free space and that the program's folder can be written to.\n\n");
            message += std::filesystem::path(error.message()).wstring();
            MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONERROR);
            return false;
        }
        return true;
    }
}
