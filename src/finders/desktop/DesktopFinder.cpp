#include "DesktopFinder.hpp"
#include "../../helpers/Log.hpp"
#include "../Fuzzy.hpp"
#include "../Cache.hpp"
#include "../../config/ConfigManager.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sys/inotify.h>
#include <sys/poll.h>
#include <unistd.h>
#include <unordered_set>

#include <hyprutils/string/String.hpp>
#include <hyprutils/os/Process.hpp>
#include <hyprutils/string/ConstVarList.hpp>

using namespace Hyprutils::String;
using namespace Hyprutils::OS;

static std::optional<std::string> readFileAsString(const std::filesystem::path& path) {
    std::error_code ec;

    if (!std::filesystem::exists(path, ec) || ec)
        return std::nullopt;

    std::ifstream file(path.string());
    if (!file.good())
        return std::nullopt;

    return trim(std::string((std::istreambuf_iterator<char>(file)), (std::istreambuf_iterator<char>())));
}

void CDesktopEntry::run() {
    static auto            PLAUNCHPREFIX = Hyprlang::CSimpleConfigValue<Hyprlang::STRING>(g_configManager->m_config.get(), "finders:desktop_launch_prefix");
    static auto            PTERMINALEXEC = Hyprlang::CSimpleConfigValue<Hyprlang::STRING>(g_configManager->m_config.get(), "finders:desktop_terminal");
    const std::string_view LAUNCH_PREFIX = *PLAUNCHPREFIX;
    const std::string_view TERMINAL_EXEC = *PTERMINALEXEC;

    auto                   toExec = std::format("{}{}{}", LAUNCH_PREFIX.empty() ? std::string{""} : std::string{LAUNCH_PREFIX} + std::string{" "},
                                                m_terminal && !TERMINAL_EXEC.empty() ? std::string{TERMINAL_EXEC} + std::string{" "} : std::string{""}, m_exec);

    Debug::log(TRACE, "Running {}", toExec);

    g_desktopFinder->m_entryFrequencyCache->incrementCachedEntry(m_name);
    m_frequency = g_desktopFinder->m_entryFrequencyCache->getCachedEntry(m_name);

    // replace all funky codes with nothing
    replaceInString(toExec, "%U", "");
    replaceInString(toExec, "%f", "");
    replaceInString(toExec, "%F", "");
    replaceInString(toExec, "%u", "");
    replaceInString(toExec, "%i", "");
    replaceInString(toExec, "%c", "");
    replaceInString(toExec, "%k", "");
    replaceInString(toExec, "%d", "");
    replaceInString(toExec, "%D", "");
    replaceInString(toExec, "%N", "");
    replaceInString(toExec, "%n", "");

    CProcess proc("/bin/sh", {"-c", toExec});
    proc.runAsync();
}

static std::vector<std::string> parseActionIdentifiers(const std::string_view actionsValue) {
    std::vector<std::string>    result;
    std::unordered_set<std::string> seen;

    size_t start = 0;
    while (start <= actionsValue.size()) {
        size_t end = actionsValue.find(';', start);
        if (end == std::string_view::npos)
            end = actionsValue.size();

        std::string segment(actionsValue.substr(start, end - start));
        segment = trim(segment);

        if (!segment.empty() && seen.insert(segment).second)
            result.emplace_back(std::move(segment));

        start = end + 1;
    }

    return result;
}

static std::optional<SDesktopAction> parseActionSection(const std::string& content, const std::string& identifier) {
    // Locate the section header: [Desktop Action <identifier>]
    const std::string sectionHeader = "[Desktop Action " + identifier + "]";
    size_t            sectionStart  = content.find(sectionHeader);

    if (sectionStart == std::string::npos) {
        Debug::log(TRACE, "desktop: action section not found for identifier \"{}\"", identifier);
        return std::nullopt;
    }

    // Move past the header line
    size_t sectionBodyStart = content.find('\n', sectionStart);
    if (sectionBodyStart == std::string::npos)
        return std::nullopt; // Header is at end of file with no body
    sectionBodyStart += 1; // skip the newline

    // Find the end of the section: next '[' at the start of a line, or EOF
    size_t sectionEnd = std::string::npos;
    size_t searchPos  = sectionBodyStart;
    while (searchPos < content.size()) {
        size_t lineStart = searchPos;
        if (content[lineStart] == '[') {
            sectionEnd = lineStart;
            break;
        }
        // Advance to next line
        size_t nextNewline = content.find('\n', searchPos);
        if (nextNewline == std::string::npos)
            break;
        searchPos = nextNewline + 1;
    }

    // Extract the section body
    std::string_view sectionBody;
    if (sectionEnd != std::string::npos)
        sectionBody = std::string_view(content).substr(sectionBodyStart, sectionEnd - sectionBodyStart);
    else
        sectionBody = std::string_view(content).substr(sectionBodyStart);

    // Helper to extract a field value from within the section body
    auto extractField = [&sectionBody](const std::string_view key) -> std::string {
        // Look for key at start of a line: "\nKey=" or at the very start of section body
        std::string searchKey = std::string(key) + "=";
        size_t      pos       = std::string_view::npos;

        // Check if section body starts with the key
        if (sectionBody.starts_with(searchKey)) {
            pos = 0;
        } else {
            std::string nlKey = "\n" + searchKey;
            size_t      found = sectionBody.find(nlKey);
            if (found != std::string_view::npos)
                pos = found + 1; // skip the \n
        }

        if (pos == std::string_view::npos)
            return "";

        // Find the '=' and extract value
        size_t eqPos = sectionBody.find('=', pos);
        if (eqPos == std::string_view::npos)
            return "";

        size_t valueStart = eqPos + 1;

        // Find end of line
        size_t valueEnd = sectionBody.find('\n', valueStart);
        if (valueEnd == std::string_view::npos)
            valueEnd = sectionBody.size();

        std::string value(sectionBody.substr(valueStart, valueEnd - valueStart));
        // Trim whitespace
        value.erase(0, value.find_first_not_of(" \t\r"));
        value.erase(value.find_last_not_of(" \t\r") + 1);
        return value;
    };

    std::string actionName = extractField("Name");
    std::string actionExec = extractField("Exec");
    std::string actionIcon = extractField("Icon");

    // Skip if Name or Exec is missing or empty after trimming
    if (actionName.empty() || actionExec.empty()) {
        Debug::log(TRACE, "desktop: skipping action \"{}\" - missing Name or Exec", identifier);
        return std::nullopt;
    }

    return SDesktopAction{.name = std::move(actionName), .exec = std::move(actionExec), .icon = std::move(actionIcon)};
}

static std::filesystem::path resolvePath(const std::string& p) {
    if (p[0] != '~')
        return p;

    const auto HOME = getenv("HOME");

    if (!HOME)
        return "";

    return std::filesystem::path(HOME) / p.substr(2);
}

CDesktopFinder::CDesktopFinder() : m_inotifyFd(inotify_init()), m_entryFrequencyCache(makeUnique<CEntryCache>("desktop")) {
    if (const auto DATA_HOME = getenv("XDG_DATA_HOME"))
        m_envPaths.emplace_back(std::filesystem::path(DATA_HOME) / "applications");
    else
        m_envPaths.emplace_back(resolvePath("~/.local/share/applications"));

    if (const auto DATA_DIRS = getenv("XDG_DATA_DIRS")) {
        CConstVarList paths(DATA_DIRS, 0, ':', false);
        for (const auto& p : paths)
            m_envPaths.emplace_back(std::filesystem::path(p) / "applications");
    } else {
        m_envPaths.emplace_back("/usr/local/share/applications");
        m_envPaths.emplace_back("/usr/share/applications");
    }
}

void CDesktopFinder::init() {
    recache();
    replantWatch();
}

void CDesktopFinder::onInotifyEvent() {
    recache();

    replantWatch();
}

void CDesktopFinder::recache() {
    m_desktopEntryPaths.clear();
    m_desktopEntryCache.clear();
    m_desktopEntryCacheGeneric.clear();

    std::unordered_set<std::string>                                                 desktopFileIds;
    std::unordered_set<std::filesystem::path>                                       directories;

    std::function<void(const std::filesystem::path&, const std::filesystem::path&)> cacheDirectory;
    cacheDirectory = [this, &cacheDirectory, &desktopFileIds, &directories](const std::filesystem::path& base, const std::filesystem::path& p) {
        std::error_code ec;
        auto            canonicalPath = std::filesystem::canonical(p, ec);
        if (ec || !directories.insert(canonicalPath).second) {
            Debug::log(TRACE, "desktop: skipping {}, does not exist / already visited", p.string());
            return;
        }
        auto it = std::filesystem::directory_iterator(p, ec);
        if (ec)
            return;
        for (const auto& e : it) {
            auto status = e.status(ec);
            if (ec)
                continue;
            if (std::filesystem::is_regular_file(status)) {
                auto relDesktopFilePath = e.path().lexically_relative(base);
                if (relDesktopFilePath.extension() != ".desktop") {
                    Debug::log(TRACE, "desktop: skipping non-desktop file at {}", e.path().string());
                    continue;
                }
                auto desktopFileId = relDesktopFilePath.string();
                std::ranges::replace(desktopFileId, '/', '-');
                if (desktopFileIds.insert(desktopFileId).second)
                    cacheEntry(e.path());
                else
                    Debug::log(TRACE, "desktop: skipping entry at {}, already cached desktopFileId {}", e.path().string(), desktopFileId);
            } else if (std::filesystem::is_directory(status))
                cacheDirectory(base, e.path());
        }

        m_desktopEntryPaths.emplace_back(p);
    };

    for (const auto& PATH : m_envPaths) {
        cacheDirectory(PATH, PATH);
    }
}

void CDesktopFinder::replantWatch() {
    for (const auto& w : m_watches) {
        inotify_rm_watch(m_inotifyFd.get(), w);
    }

    m_watches.clear();

    while (true) {
        pollfd pfd = {
            .fd     = m_inotifyFd.get(),
            .events = POLLIN,
        };

        poll(&pfd, 1, 0);

        if (!(pfd.revents & POLLIN))
            break;

        static char buf[1024];

        read(m_inotifyFd.get(), buf, 1023);
    }

    for (const auto& p : m_desktopEntryPaths) {
        m_watches.emplace_back(inotify_add_watch(m_inotifyFd.get(), p.c_str(), IN_MODIFY | IN_DONT_FOLLOW | IN_CREATE | IN_DELETE | IN_MOVE));
    }
}

void CDesktopFinder::cacheEntry(const std::filesystem::path& path) {
    Debug::log(TRACE, "desktop: caching entry at {}", path.string());

    const auto READ_RESULT = readFileAsString(path);

    if (!READ_RESULT)
        return;

    const auto& DATA = *READ_RESULT;

    auto        extract = [&DATA](const std::string_view what) -> std::string_view {
        size_t begins = DATA.find("\n" + std::string{what} + " ");

        if (begins == std::string::npos)
            begins = DATA.find("\n" + std::string{what} + "=");

        if (begins == std::string::npos)
            return "";

        begins = DATA.find('=', begins);

        if (begins == std::string::npos)
            return "";

        begins += 1; // eat the equals
        while (begins < DATA.size() && std::isspace(DATA[begins])) {
            ++begins;
        }

        size_t ends = DATA.find("\n", begins + 1);

        if (!ends)
            return std::string_view{DATA}.substr(begins);

        return std::string_view{DATA}.substr(begins, ends - begins);
    };

    const auto NAME      = extract("Name");
    const auto GEN_NAME  = extract("GenericName");
    const auto ICON      = extract("Icon");
    const auto EXEC      = extract("Exec");
    const auto NODISPLAY = extract("NoDisplay") == "true";
    const auto TERMINAL  = extract("Terminal") == "true";

    if (EXEC.empty() || NAME.empty() || NODISPLAY) {
        Debug::log(TRACE, "desktop: skipping entry, empty name / exec / NoDisplay");
        return;
    }

    auto pathStem = path.stem().string();

    if (path.string().starts_with("/home")) {
        // home paths should override system ones
        std::erase_if(m_desktopEntryCache, [&pathStem](const auto& e) { return e->m_stem == pathStem; });
    }

    auto& e        = m_desktopEntryCache.emplace_back(makeShared<CDesktopEntry>());
    e->m_exec      = EXEC;
    e->m_icon      = ICON;
    e->m_name      = NAME;
    e->m_stem      = std::move(pathStem);
    e->m_terminal  = TERMINAL;
    e->m_frequency = m_entryFrequencyCache->getCachedEntry(e->m_name);

    Debug::log(TRACE, "desktop: cached {} with icon {} and exec line of \"{}\"", NAME, ICON, EXEC);

    // Parse Actions= key for desktop action support
    const auto ACTIONS = extract("Actions");
    if (!ACTIONS.empty()) {
        auto actionIdentifiers = parseActionIdentifiers(ACTIONS);
        Debug::log(TRACE, "desktop: found {} action identifiers for {}", actionIdentifiers.size(), NAME);

        for (const auto& id : actionIdentifiers) {
            auto action = parseActionSection(DATA, id);
            if (action) {
                Debug::log(TRACE, "desktop: parsed action \"{}\" for {}", action->name, NAME);
                e->m_actions.emplace_back(std::move(*action));
            }
        }
    }

    // Build fuzzables: Name, GenericName, plus all action names
    // Fuzzy::createFuzzableStrings takes an initializer_list which can't be built dynamically,
    // so we build the vector manually with the same lowercase transformation.
    {
        std::vector<std::string> fuzzables;
        fuzzables.reserve(2 + e->m_actions.size());

        auto toLower = [](std::string_view sv) -> std::string {
            std::string result;
            result.resize(sv.size());
            std::ranges::transform(sv, result.begin(), ::tolower);
            return result;
        };

        fuzzables.emplace_back(toLower(NAME));
        fuzzables.emplace_back(toLower(GEN_NAME));

        for (const auto& action : e->m_actions) {
            fuzzables.emplace_back(toLower(action.name));
        }

        e->m_fuzzables = std::move(fuzzables);
    }

    m_desktopEntryCacheGeneric.emplace_back(e);
}

void executeDesktopAction(const SDesktopAction& action, bool parentTerminal) {
    static auto            PLAUNCHPREFIX = Hyprlang::CSimpleConfigValue<Hyprlang::STRING>(g_configManager->m_config.get(), "finders:desktop_launch_prefix");
    static auto            PTERMINALEXEC = Hyprlang::CSimpleConfigValue<Hyprlang::STRING>(g_configManager->m_config.get(), "finders:desktop_terminal");
    const std::string_view LAUNCH_PREFIX = *PLAUNCHPREFIX;
    const std::string_view TERMINAL_EXEC = *PTERMINALEXEC;

    auto                   toExec = std::format("{}{}{}", LAUNCH_PREFIX.empty() ? std::string{""} : std::string{LAUNCH_PREFIX} + std::string{" "},
                                                parentTerminal && !TERMINAL_EXEC.empty() ? std::string{TERMINAL_EXEC} + std::string{" "} : std::string{""}, action.exec);

    // replace all funky codes with nothing
    replaceInString(toExec, "%U", "");
    replaceInString(toExec, "%u", "");
    replaceInString(toExec, "%f", "");
    replaceInString(toExec, "%F", "");
    replaceInString(toExec, "%i", "");
    replaceInString(toExec, "%c", "");
    replaceInString(toExec, "%k", "");
    replaceInString(toExec, "%d", "");
    replaceInString(toExec, "%D", "");
    replaceInString(toExec, "%N", "");
    replaceInString(toExec, "%n", "");

    Debug::log(TRACE, "Running desktop action: {}", toExec);

    CProcess proc("/bin/sh", {"-c", toExec});
    proc.runAsync();
}

std::vector<SFinderResult> CDesktopFinder::getResultsForQuery(const std::string& query) {
    static auto                PICONSENABLED = Hyprlang::CSimpleConfigValue<Hyprlang::INT>(g_configManager->m_config.get(), "finders:desktop_icons");

    auto                       fuzzed = Fuzzy::getNResults(m_desktopEntryCacheGeneric, query, MAX_RESULTS_PER_FINDER);

    // Build final results
    std::vector<SFinderResult> results;
    results.reserve(fuzzed.size());

    for (const auto& r : fuzzed) {
        const auto p = reinterpretPointerCast<CDesktopEntry>(r);
        if (!p)
            continue;
        results.emplace_back(SFinderResult{
            .label      = p->m_name,
            .icon       = *PICONSENABLED ? p->m_icon : "",
            .result     = p,
            .hasIcon    = true,
            .hasSubmenu = p->hasActions(),
        });
    }

    return results;
}
