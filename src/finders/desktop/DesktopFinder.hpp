#pragma once

#include "../IFinder.hpp"

#include <hyprutils/os/FileDescriptor.hpp>
#include <filesystem>
#include <string>
#include <vector>

struct SDesktopAction {
    std::string name; // from Name= in [Desktop Action] section
    std::string exec; // from Exec= in [Desktop Action] section
    std::string icon; // from Icon= in [Desktop Action] section (may be empty)
};

class CEntryCache;

class CDesktopEntry : public IFinderResult {
  public:
    CDesktopEntry()          = default;
    virtual ~CDesktopEntry() = default;

    virtual const std::vector<std::string>& fuzzables() {
        return m_fuzzables;
    }

    virtual eFinderTypes type() {
        return FINDER_DESKTOP;
    }

    virtual uint32_t frequency() {
        return m_frequency;
    }

    virtual const std::string& name() {
        return m_name;
    }

    virtual void run();

    bool hasActions() const {
        return !m_actions.empty();
    }

    std::string              m_name, m_exec, m_icon, m_stem;
    std::vector<std::string> m_fuzzables;
    bool                     m_terminal = false;

    uint32_t                 m_frequency = 0;

    std::vector<SDesktopAction> m_actions;
};

class CDesktopFinder : public IFinder {
  public:
    CDesktopFinder();
    virtual ~CDesktopFinder() = default;

    virtual std::vector<SFinderResult> getResultsForQuery(const std::string& query);
    virtual void                       init();

    Hyprutils::OS::CFileDescriptor     m_inotifyFd;

    void                               onInotifyEvent();

  private:
    std::vector<SP<CDesktopEntry>>     m_desktopEntryCache;
    std::vector<SP<IFinderResult>>     m_desktopEntryCacheGeneric;

    std::vector<std::filesystem::path> m_desktopEntryPaths;
    std::vector<int>                   m_watches;

    std::vector<std::filesystem::path> m_envPaths;

    UP<CEntryCache>                    m_entryFrequencyCache;

    void                               cacheEntry(const std::filesystem::path& path);
    void                               replantWatch();
    void                               recache();

    friend class CDesktopEntry;
};

inline UP<CDesktopFinder> g_desktopFinder;

void executeDesktopAction(const SDesktopAction& action, bool parentTerminal);
