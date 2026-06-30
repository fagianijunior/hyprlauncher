#include "UI.hpp"
#include "ResultButton.hpp"

#include "../finders/desktop/DesktopFinder.hpp"
#include "../query/QueryProcessor.hpp"
#include "../socket/ServerSocket.hpp"
#include "../helpers/Log.hpp"
#include "../config/ConfigManager.hpp"
#include "../i18n/Engine.hpp"

#include <hyprutils/string/String.hpp>
#include <xkbcommon/xkbcommon-keysyms.h>
#include <algorithm>

using namespace Hyprutils::Math;
using namespace Hyprutils::String;

constexpr const size_t MAX_RESULTS_IN_LAUNCHER = 50;

CUI::CUI(bool open) : m_openByDefault(open) {
    static auto PGRABFOCUS  = Hyprlang::CSimpleConfigValue<Hyprlang::INT>(g_configManager->m_config.get(), "general:grab_focus");
    static auto PWINDOWSIZE = Hyprlang::CSimpleConfigValue<Hyprlang::VEC2>(g_configManager->m_config.get(), "ui:window_size");

    m_backend = Hyprtoolkit::IBackend::create();

    m_background = Hyprtoolkit::CRectangleBuilder::begin()
                       ->color([this] { return m_backend->getPalette()->m_colors.background; })
                       ->rounding(m_backend->getPalette()->m_vars.bigRounding)
                       ->borderColor([this] { return m_backend->getPalette()->m_colors.accent.darken(0.2F); })
                       ->borderThickness(1)
                       ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1, 1}})
                       ->commence();

    m_layout =
        Hyprtoolkit::CColumnLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1, 1}})->gap(4)->commence();
    m_layout->setMargin(4);

    m_inputBox = Hyprtoolkit::CTextboxBuilder::begin()
                     ->placeholder(I18n::localize(I18n::TXT_KEY_SEARCH_SOMETHING, {}))
                     ->onTextEdited([this](SP<Hyprtoolkit::CTextboxElement>, const std::string& query) {
                         m_lastQuery = query;
                         g_queryProcessor->scheduleQueryUpdate(query);
                     })
                     ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 28.F}})
                     ->multiline(false)
                     ->commence();

    m_hr = Hyprtoolkit::CRectangleBuilder::begin()
               ->color([this] { return m_backend->getPalette()->m_colors.accent.darken(0.2F); })
               ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {0.8F, 1.F}})
               ->commence();
    m_hr->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
    m_hr->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_HCENTER, true);

    m_scrollArea = Hyprtoolkit::CScrollAreaBuilder::begin()
                       ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, 10.F}})
                       ->scrollY(true)
                       ->commence();
    m_scrollArea->setGrow(true);

    m_resultsLayout =
        Hyprtoolkit::CColumnLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_AUTO, {1, 1}})->gap(2)->commence();

    m_background->addChild(m_layout);

    m_layout->addChild(m_inputBox);
    m_layout->addChild(m_hr);
    m_layout->addChild(m_scrollArea);

    m_scrollArea->addChild(m_resultsLayout);

    //
    m_window = Hyprtoolkit::CWindowBuilder::begin()
                   ->appClass("hyprlauncher")
                   ->type(Hyprtoolkit::HT_WINDOW_LAYER)
                   ->preferredSize({(*PWINDOWSIZE).x, (*PWINDOWSIZE).y})
                   ->anchor(1 | 2 | 4 | 8)
                   ->exclusiveZone(-1)
                   ->layer(3)
                   ->kbInteractive(*PGRABFOCUS ? 1 : 2)
                   ->commence();

    m_window->m_rootElement->addChild(m_background);

    m_window->m_events.keyboardKey.listenStatic([this](Hyprtoolkit::Input::SKeyboardKeyEvent e) {
        if (e.xkbKeysym == XKB_KEY_Escape)
            setWindowOpen(false);
        else if (e.xkbKeysym == XKB_KEY_Down) {
            const size_t totalVisible = m_currentResults.size() + m_submenuInsertOffset;
            if (m_activeElementId + 1 < totalVisible)
                m_activeElementId++;
            updateActive();
        } else if (e.xkbKeysym == XKB_KEY_Up) {
            if (m_activeElementId > 0)
                m_activeElementId--;
            updateActive();
        } else if (e.xkbKeysym == XKB_KEY_Right) {
            // Expand submenu if active item is a main result with actions
            if (!isSubmenuItemIdx(m_activeElementId)) {
                // Compute the real result index accounting for submenu offset
                size_t resultIdx = m_activeElementId;
                if (m_expandedParentIdx.has_value() && m_activeElementId > *m_expandedParentIdx + m_submenuInsertOffset)
                    resultIdx = m_activeElementId - m_submenuInsertOffset;

                if (resultIdx < m_currentResults.size() && m_currentResults[resultIdx].hasSubmenu)
                    expandSubmenu(resultIdx);
            }
        } else if (e.xkbKeysym == XKB_KEY_Left) {
            // Collapse submenu and return selection to parent
            if (m_expandedParentIdx.has_value()) {
                m_activeElementId = *m_expandedParentIdx;
                collapseSubmenu();
            }
        } else if (e.xkbKeysym == XKB_KEY_Return || e.xkbKeysym == XKB_KEY_KP_Enter)
            onSelected();
    });
}

CUI::~CUI() = default;

void CUI::run() {
    m_resultButtons.reserve(MAX_RESULTS_IN_LAUNCHER);
    for (size_t i = 0; i < MAX_RESULTS_IN_LAUNCHER; ++i) {
        auto b     = m_resultButtons.emplace_back(makeShared<CResultButton>());
        b->m_added = true;
        m_resultsLayout->addChild(b->m_background);
    }

    if (m_openByDefault)
        setWindowOpen(true);

    if (g_serverIPCSocket->m_socket) {
        m_backend->addFd(g_serverIPCSocket->m_socket->extractLoopFD(), [] {
            Debug::log(TRACE, "got an ipc event");
            g_serverIPCSocket->m_socket->dispatchEvents(false);
        });
    }

    m_backend->addFd(g_configManager->m_inotifyFd.get(), [] { g_configManager->onInotifyEvent(); });
    m_backend->addFd(g_desktopFinder->m_inotifyFd.get(), [] { g_desktopFinder->onInotifyEvent(); });

    m_backend->enterLoop();
}

void CUI::setWindowOpen(bool open) {
    if (open == m_open)
        return;

    m_open = open;

    if (open) {
        m_inputBox->rebuild()->defaultText("")->commence();

        updateResults({});

        m_window->open();

        m_inputBox->focus();

        g_queryProcessor->scheduleQueryUpdate("");
    } else {
        m_window->close();
        g_queryProcessor->overrideQueryProvider(WP<IFinder>{});
    }

    g_serverIPCSocket->sendOpenState(open);
}

void CUI::onSelected() {
    const size_t totalVisible = m_currentResults.size() + m_submenuInsertOffset;
    if (m_activeElementId >= totalVisible)
        return;

    if (isSubmenuItemIdx(m_activeElementId)) {
        // Submenu item selected — execute the desktop action
        const size_t actionIdx = submenuActionIndex(m_activeElementId);
        if (actionIdx >= m_submenuActions.size())
            return;

        // Get the parent entry to check its terminal flag
        const auto parentEntry = reinterpretPointerCast<CDesktopEntry>(m_currentResults[*m_expandedParentIdx].result);
        bool       parentTerminal = parentEntry ? parentEntry->m_terminal : false;

        g_serverIPCSocket->sendSelectionMade(m_submenuActions[actionIdx].name);
        executeDesktopAction(m_submenuActions[actionIdx], parentTerminal);
        setWindowOpen(false);
        g_queryProcessor->overrideQueryProvider(WP<IFinder>{});
    } else {
        // Main result selected — compute real result index
        size_t resultIdx = m_activeElementId;
        if (m_expandedParentIdx.has_value() && m_activeElementId > *m_expandedParentIdx + m_submenuInsertOffset)
            resultIdx = m_activeElementId - m_submenuInsertOffset;

        if (resultIdx >= m_currentResults.size())
            return;

        g_serverIPCSocket->sendSelectionMade(m_currentResults.at(resultIdx).result->name());
        m_currentResults.at(resultIdx).result->run();
        setWindowOpen(false);
        g_queryProcessor->overrideQueryProvider(WP<IFinder>{});
    }
}

bool CUI::windowOpen() {
    return m_open;
}

void CUI::collapseSubmenu() {
    if (!m_expandedParentIdx.has_value()) {
        // Nothing expanded, just ensure state is clean
        m_submenuActions.clear();
        m_submenuInsertOffset = 0;
        return;
    }

    m_submenuActions.clear();
    m_expandedParentIdx.reset();
    m_submenuInsertOffset = 0;

    // Re-render all buttons from m_currentResults (normal display)
    for (size_t i = 0; i < m_resultButtons.size(); ++i) {
        auto& btn = m_resultButtons[i];
        if (i >= m_currentResults.size()) {
            btn->setLabel("", "", std::nullopt, false);
        } else {
            btn->setLabel(m_currentResults[i].label, m_currentResults[i].icon, m_currentResults[i].overrideFont, m_currentResults[i].hasIcon);
        }
        btn->setIndented(false);
        btn->setSubmenuStyle(false);
        btn->setSubmenuIndicator(i < m_currentResults.size() && m_currentResults[i].hasSubmenu);
    }

    updateActive();
}

void CUI::expandSubmenu(size_t parentIdx) {
    // Collapse any currently expanded submenu first
    if (m_expandedParentIdx.has_value())
        collapseSubmenu();

    // Bounds check
    if (parentIdx >= m_currentResults.size())
        return;

    // Cast to CDesktopEntry to access m_actions
    const auto entry = reinterpretPointerCast<CDesktopEntry>(m_currentResults[parentIdx].result);
    if (!entry || !entry->hasActions())
        return;

    // Copy up to 10 actions into m_submenuActions
    const size_t actionCount = std::min(entry->m_actions.size(), size_t{10});
    m_submenuActions.clear();
    m_submenuActions.reserve(actionCount);
    for (size_t i = 0; i < actionCount; ++i) {
        m_submenuActions.emplace_back(entry->m_actions[i]);
    }

    m_expandedParentIdx   = parentIdx;
    m_submenuInsertOffset = m_submenuActions.size();

    // Re-render all buttons to reflect the new visible list composition:
    // [results 0..parentIdx] [submenu items parentIdx+1..parentIdx+offset] [remaining results parentIdx+offset+1..]
    const size_t totalVisible = m_currentResults.size() + m_submenuInsertOffset;

    for (size_t i = 0; i < m_resultButtons.size(); ++i) {
        auto& btn = m_resultButtons[i];

        if (i >= totalVisible) {
            // Beyond visible range: clear the button
            btn->setLabel("", "", std::nullopt, false);
            btn->setIndented(false);
            btn->setSubmenuStyle(false);
            btn->setSubmenuIndicator(false);
        } else if (i <= parentIdx) {
            // Main results before and including parent
            const auto& res = m_currentResults[i];
            btn->setLabel(res.label, res.icon, res.overrideFont, res.hasIcon);
            btn->setIndented(false);
            btn->setSubmenuStyle(false);
            // Parent itself loses the submenu indicator while expanded; others keep theirs
            btn->setSubmenuIndicator(i == parentIdx ? false : res.hasSubmenu);
        } else if (i <= parentIdx + m_submenuInsertOffset) {
            // Submenu items
            size_t      actionIdx = i - parentIdx - 1;
            const auto& action    = m_submenuActions[actionIdx];
            // Use action icon if available, otherwise fall back to parent icon
            const std::string& icon = action.icon.empty() ? entry->m_icon : action.icon;
            btn->setLabel(action.name, icon, std::nullopt, true);
            btn->setIndented(true);
            btn->setSubmenuStyle(true);
            btn->setSubmenuIndicator(false);
        } else {
            // Remaining main results shifted down
            size_t resultIdx = i - m_submenuInsertOffset;
            if (resultIdx < m_currentResults.size()) {
                const auto& res = m_currentResults[resultIdx];
                btn->setLabel(res.label, res.icon, res.overrideFont, res.hasIcon);
                btn->setIndented(false);
                btn->setSubmenuStyle(false);
                btn->setSubmenuIndicator(res.hasSubmenu);
            } else {
                btn->setLabel("", "", std::nullopt, false);
                btn->setIndented(false);
                btn->setSubmenuStyle(false);
                btn->setSubmenuIndicator(false);
            }
        }
    }

    // Auto-select matching action: fuzzy-match m_lastQuery against submenu items
    if (!m_lastQuery.empty()) {
        // Convert query to lowercase for comparison
        std::string queryLower = m_lastQuery;
        std::ranges::transform(queryLower, queryLower.begin(), ::tolower);

        size_t bestIdx    = 0;
        bool   foundMatch = false;

        // First pass: check starts_with
        for (size_t i = 0; i < m_submenuActions.size(); ++i) {
            std::string actionNameLower = m_submenuActions[i].name;
            std::ranges::transform(actionNameLower, actionNameLower.begin(), ::tolower);
            if (actionNameLower.starts_with(queryLower)) {
                bestIdx    = i;
                foundMatch = true;
                break;
            }
        }

        // Second pass: check contains
        if (!foundMatch) {
            for (size_t i = 0; i < m_submenuActions.size(); ++i) {
                std::string actionNameLower = m_submenuActions[i].name;
                std::ranges::transform(actionNameLower, actionNameLower.begin(), ::tolower);
                if (actionNameLower.find(queryLower) != std::string::npos) {
                    bestIdx    = i;
                    foundMatch = true;
                    break;
                }
            }
        }

        // Select the best matching action, or first if no match
        m_activeElementId = parentIdx + 1 + bestIdx;
    } else {
        // No query — select first submenu item
        m_activeElementId = parentIdx + 1;
    }

    updateActive();
}

bool CUI::isSubmenuItemIdx(size_t visibleIdx) const {
    if (!m_expandedParentIdx.has_value())
        return false;
    return visibleIdx > *m_expandedParentIdx && visibleIdx <= *m_expandedParentIdx + m_submenuInsertOffset;
}

size_t CUI::submenuActionIndex(size_t visibleIdx) const {
    return visibleIdx - *m_expandedParentIdx - 1;
}

void CUI::updateResults(std::vector<SFinderResult>&& results) {
    collapseSubmenu();

    // Store the current query for auto-select matching when expanding submenus
    m_lastQuery = std::string(m_inputBox->currentText());

    m_currentResults = std::move(results);

    m_activeElementId = 0;

    for (size_t i = 0; i < m_resultButtons.size(); ++i) {
        if (m_currentResults.size() <= i) {
            m_resultButtons[i]->setLabel("", "", std::nullopt, false);
            m_resultButtons[i]->setSubmenuIndicator(false);
            m_resultButtons[i]->setIndented(false);
            m_resultButtons[i]->setSubmenuStyle(false);
        } else {
            m_resultButtons[i]->setLabel(m_currentResults[i].label, m_currentResults[i].icon, m_currentResults[i].overrideFont, m_currentResults[i].hasIcon);
            m_resultButtons[i]->setSubmenuIndicator(m_currentResults[i].hasSubmenu);
            m_resultButtons[i]->setIndented(false);
            m_resultButtons[i]->setSubmenuStyle(false);
        }
    }

    updateActive();
}

void CUI::updateActive() {
    const size_t totalVisible = m_currentResults.size() + m_submenuInsertOffset;

    for (size_t i = 0; i < m_resultButtons.size(); ++i) {
        auto& b = m_resultButtons[i];
        b->setActive(i == m_activeElementId);
        if (i >= totalVisible && b->m_added)
            m_resultsLayout->removeChild(b->m_background);
        else if (i < totalVisible && !b->m_added)
            m_resultsLayout->addChild(b->m_background);
        b->m_added = i < totalVisible;
    }

    // fit the scroll area
    const float CURRENT_SCROLL_Y = m_scrollArea->getCurrentScroll().y;
    const float BUTTON_HEIGHT    = m_resultButtons[0]->m_background->size().y + 2 /* gap */;

    const float MIN_SCROLL_TO_SEE = (BUTTON_HEIGHT * (m_activeElementId + 1)) - (m_scrollArea->size().y);
    const float MAX_SCROLL_TO_SEE = (BUTTON_HEIGHT * m_activeElementId);

    if (MAX_SCROLL_TO_SEE <= MIN_SCROLL_TO_SEE)
        return; // wtf??

    m_scrollArea->setScroll({0.F, std::clamp(CURRENT_SCROLL_Y, MIN_SCROLL_TO_SEE, MAX_SCROLL_TO_SEE)});
}
