#include "config.h"
#include "guest.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

static const char kFileName[] = "crystal_defenders.ini";

static const char kDefaultFile[] =
    "; Crystal Defenders - reglages\n"
    "; Modifier une valeur, enregistrer, puis relancer le jeu.\n"
    "; Supprimer ce fichier le recree avec les valeurs d'origine.\n"
    "\n"
    "[affichage]\n"
    "; 1 = demarrer en plein ecran (Alt+Entree ou F11 pour changer en jeu)\n"
    "plein_ecran = 0\n"
    "; Taille de l'image dans la fenetre, par exemple 1920x1080. auto = 1280x720\n"
    "; a l'echelle d'affichage de Windows. Se regle aussi dans le menu (F1).\n"
    "fenetre = auto\n"
    "; Le jeu avance d'un pas par image : 60 est sa vitesse d'origine, 30 le\n"
    "; ralentit de moitie, 120 le fait aller deux fois plus vite.\n"
    "images_par_seconde = 60\n"
    "; net   = image calculee a la resolution de l'ecran, pixels nets\n"
    "; lisse = image d'origine en 1280x720 agrandie, plus douce\n"
    "rendu = net\n"
    "\n"
    "[son]\n"
    "; Volume general, de 0 a 100. Le jeu a aussi son propre reglage dans\n"
    "; Aide et options > Parametres.\n"
    "volume = 100\n"
    "\n"
    "[jeu]\n"
    "; auto = langue de Windows. Sinon : fr, en, de, es, it, ja\n"
    "langue = auto\n"
    "; 1 = vibrations de la manette, 0 = aucune\n"
    "vibration = 1\n"
    "; 1 = le jeu se met en pause quand sa fenetre n'est plus au premier plan\n"
    "pause_arriere_plan = 1\n"
    "; 1 = afficher l'avertissement \"Non connecte a Xbox LIVE\" au demarrage,\n"
    "; comme sur la console hors ligne. 0 = le jeu se croit connecte : pas\n"
    "; d'avertissement, et l'ecran Classement reste vide.\n"
    "avertissement_live = 0\n"
    "; 1 = le jeu demande au demarrage ou enregistrer, comme sur la console.\n"
    "; 0 = il utilise directement le dossier saves.\n"
    "choix_stockage = 0\n"
    "; 1 = ecran de lancement avant le jeu (reglages, succes, captures).\n"
    "; 0 = le jeu demarre directement.\n"
    "lanceur = 1\n"
    "\n"
    "[clavier]\n"
    "; Une ou plusieurs touches par bouton de la manette, separees par un espace.\n"
    "; Touches : une lettre ou un chiffre, Haut Bas Gauche Droite, Entree, Espace,\n"
    "; Echap, Tab, Retour, Maj, Ctrl, F1 a F10, Num0 a Num9.\n"
    "; F11 et Alt+Entree restent reserves au plein ecran.\n"
    "haut = Haut\n"
    "bas = Bas\n"
    "gauche = Gauche\n"
    "droite = Droite\n"
    "a = Espace X\n"
    "b = Echap C\n"
    "x = S\n"
    "y = D\n"
    "start = Entree\n"
    "back = Tab\n"
    "lb = Q\n"
    "rb = E\n";

static std::string Lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return char(tolower(c)); });
    return text;
}

static std::string Trim(const std::string& text)
{
    size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

// Key names in French and English, case-insensitive.
static int ParseKey(const std::string& name)
{
    static const struct { const char* name; int key; } names[] =
    {
        { "haut", VK_UP }, { "up", VK_UP }, { "bas", VK_DOWN }, { "down", VK_DOWN },
        { "gauche", VK_LEFT }, { "left", VK_LEFT }, { "droite", VK_RIGHT }, { "right", VK_RIGHT },
        { "entree", VK_RETURN }, { "enter", VK_RETURN }, { "espace", VK_SPACE }, { "space", VK_SPACE },
        { "echap", VK_ESCAPE }, { "escape", VK_ESCAPE }, { "esc", VK_ESCAPE }, { "tab", VK_TAB },
        { "retour", VK_BACK }, { "backspace", VK_BACK }, { "maj", VK_SHIFT }, { "shift", VK_SHIFT },
        { "ctrl", VK_CONTROL },
    };
    for (auto& entry : names)
    {
        if (name == entry.name)
            return entry.key;
    }
    if (name.size() == 1 && ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= '0' && name[0] <= '9')))
        return toupper(name[0]);
    if (name.size() >= 2 && name[0] == 'f')
    {
        int number = atoi(name.c_str() + 1);
        if (number >= 1 && number <= 10)
            return VK_F1 + number - 1;
    }
    if (name.size() == 4 && name.compare(0, 3, "num") == 0 && name[3] >= '0' && name[3] <= '9')
        return VK_NUMPAD0 + (name[3] - '0');
    return 0;
}

static uint16_t ParseButton(const std::string& name)
{
    static const struct { const char* name; uint16_t button; } names[] =
    {
        { "haut", 0x0001 }, { "bas", 0x0002 }, { "gauche", 0x0004 }, { "droite", 0x0008 },
        { "start", 0x0010 }, { "back", 0x0020 }, { "lb", 0x0100 }, { "rb", 0x0200 },
        { "a", 0x1000 }, { "b", 0x2000 }, { "x", 0x4000 }, { "y", 0x8000 },
    };
    for (auto& entry : names)
    {
        if (name == entry.name)
            return entry.button;
    }
    return 0;
}

static void Apply(Config& config, const std::string& section, const std::string& key, const std::string& value)
{
    if (section == "affichage" && key == "plein_ecran")
        config.fullscreen = atoi(value.c_str()) != 0;
    else if (section == "affichage" && key == "fenetre")
    {
        unsigned width = 0, height = 0;
        if (sscanf(value.c_str(), "%ux%u", &width, &height) != 2 || width < 320 || height < 180 || width > 16384 || height > 16384)
            width = height = 0;
        config.windowWidth = width;
        config.windowHeight = height;
    }
    else if (section == "affichage" && key == "images_par_seconde")
        config.framesPerSecond = uint32_t(std::clamp(atoi(value.c_str()), 15, 240));
    else if (section == "affichage" && key == "rendu")
        config.sharp = value != "lisse";
    else if (section == "souris")
        ; // from an earlier version, no longer used
    else if (section == "son" && key == "volume")
        config.volume = std::clamp(atoi(value.c_str()), 0, 100);
    else if (section == "jeu" && key == "pause_arriere_plan")
        config.pauseInBackground = atoi(value.c_str()) != 0;
    else if (section == "jeu" && key == "vibration")
        config.vibration = atoi(value.c_str()) != 0;
    else if (section == "jeu" && key == "avertissement_live")
        config.liveWarning = atoi(value.c_str()) != 0;
    else if (section == "jeu" && key == "choix_stockage")
        config.storageDialog = atoi(value.c_str()) != 0;
    else if (section == "jeu" && key == "lanceur")
        config.launcher = atoi(value.c_str()) != 0;
    else if (section == "jeu" && key == "langue")
    {
        static const struct { const char* name; uint32_t id; } languages[] =
        {
            { "en", 1 }, { "ja", 2 }, { "de", 3 }, { "fr", 4 }, { "es", 5 }, { "it", 6 },
        };
        config.language = 0;
        for (auto& language : languages)
        {
            if (value == language.name)
                config.language = language.id;
        }
        if (config.language == 0 && value != "auto")
            Log("Config: unknown language '%s', following Windows", value.c_str());
    }
    else if (section == "clavier")
    {
        uint16_t button = ParseButton(key);
        if (button == 0)
        {
            Log("Config: unknown button '%s'", key.c_str());
            return;
        }
        // A button listed in the file replaces its default keys.
        std::erase_if(config.keys, [button](const KeyBinding& binding) { return binding.button == button; });
        size_t position = 0;
        while (position < value.size())
        {
            size_t end = value.find_first_of(" \t,", position);
            if (end == std::string::npos)
                end = value.size();
            if (end > position)
            {
                std::string name = value.substr(position, end - position);
                if (int code = ParseKey(name))
                    config.keys.push_back({ code, button });
                else
                    Log("Config: unknown key '%s' for button '%s'", name.c_str(), key.c_str());
            }
            position = end + 1;
        }
    }
    else
    {
        Log("Config: unknown setting [%s] %s", section.c_str(), key.c_str());
    }
}

static void Parse(Config& config, const char* text)
{
    std::string section;
    for (const char* line = text; *line != 0;)
    {
        const char* end = strchr(line, '\n');
        std::string content = Trim(end ? std::string(line, end) : std::string(line));
        line = end ? end + 1 : line + strlen(line);

        if (content.empty() || content[0] == ';' || content[0] == '#')
            continue;
        if (content.front() == '[' && content.back() == ']')
        {
            section = Lower(Trim(content.substr(1, content.size() - 2)));
            continue;
        }
        size_t equals = content.find('=');
        if (equals != std::string::npos)
            Apply(config, section, Lower(Trim(content.substr(0, equals))), Lower(Trim(content.substr(equals + 1))));
    }
}

static Config Load()
{
    Config config;
    Parse(config, kDefaultFile);

#ifndef CD_DARKLAUNCHER // DarkLauncher keeps the settings in its own file (see darklauncher.cpp)
    std::string text;
    if (FILE* file = fopen(kFileName, "rb"))
    {
        char buffer[4096];
        size_t read;
        while ((read = fread(buffer, 1, sizeof(buffer), file)) != 0)
            text.append(buffer, read);
        fclose(file);
        if (text.compare(0, 3, "\xEF\xBB\xBF") == 0)
            text.erase(0, 3);
        Parse(config, text.c_str());
    }
    else if (FILE* created = fopen(kFileName, "wb"))
    {
        fwrite(kDefaultFile, 1, sizeof(kDefaultFile) - 1, created);
        fclose(created);
        Log("Config: %s created with the default settings", kFileName);
    }
#endif

    if (OnSteamDeck())
    {
        config.fullscreen = true;
        config.pauseInBackground = false;
        Log("Config: Steam Deck detected, full screen and no background pause");
    }
    Log("Config: fullscreen %d, sharp %d, volume %d, language %u, vibration %d, %zu key binding(s)", config.fullscreen,
        config.sharp, config.volume, config.language, config.vibration, config.keys.size());
    return config;
}

bool OnSteamDeck()
{
    static const bool deck = [] { const char* value = getenv("SteamDeck"); return value != nullptr && strcmp(value, "1") == 0; }();
    return deck;
}

Config& MutableConfig()
{
    static Config config = Load();
    return config;
}

const Config& GetConfig()
{
    return MutableConfig();
}

void ConfigSave(const char* section, const char* key, const char* value)
{
#ifdef CD_DARKLAUNCHER
    (void)section, (void)key, (void)value;
    return; // DarkLauncher writes the settings
#endif
    std::string text;
    if (FILE* file = fopen(kFileName, "rb"))
    {
        char buffer[4096];
        size_t read;
        while ((read = fread(buffer, 1, sizeof(buffer), file)) != 0)
            text.append(buffer, read);
        fclose(file);
    }
    else
    {
        text = kDefaultFile;
    }

    // Replace the key's line inside its section; add the line, or the whole
    // section, if the file does not have it.
    const std::string line = std::string(key) + " = " + value;
    std::string result;
    std::string current;
    bool sectionSeen = false;
    bool done = false;
    size_t position = 0;
    while (position < text.size())
    {
        size_t end = text.find('\n', position);
        std::string raw = text.substr(position, end == std::string::npos ? std::string::npos : end - position);
        position = end == std::string::npos ? text.size() : end + 1;
        std::string content = Trim(raw);

        if (!content.empty() && content.front() == '[' && content.back() == ']')
        {
            if (current == section && !done)
            {
                result += line + "\n";
                done = true;
            }
            current = Lower(Trim(content.substr(1, content.size() - 2)));
            sectionSeen = sectionSeen || current == section;
        }
        else if (!done && current == section && !content.empty() && content[0] != ';' && content[0] != '#')
        {
            size_t equals = content.find('=');
            if (equals != std::string::npos && Lower(Trim(content.substr(0, equals))) == key)
            {
                raw = line;
                done = true;
            }
        }
        result += raw + "\n";
    }
    if (!done)
        result += (sectionSeen && current == section ? "" : "\n[" + std::string(section) + "]\n") + line + "\n";

    if (FILE* file = fopen(kFileName, "wb"))
    {
        fwrite(result.data(), 1, result.size(), file);
        fclose(file);
    }
}
