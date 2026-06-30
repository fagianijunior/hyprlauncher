#pragma once

#include <hyprtoolkit/core/Backend.hpp>
#include <hyprtoolkit/window/Window.hpp>
#include <hyprtoolkit/element/Rectangle.hpp>
#include <hyprtoolkit/element/Text.hpp>
#include <hyprtoolkit/element/ColumnLayout.hpp>
#include <hyprtoolkit/element/RowLayout.hpp>
#include <hyprtoolkit/element/Null.hpp>
#include <hyprtoolkit/element/Button.hpp>
#include <hyprtoolkit/element/ScrollArea.hpp>
#include <hyprtoolkit/element/Textbox.hpp>

#include <hyprtoolkit/system/Icons.hpp>

#include <optional>

#include "../helpers/Memory.hpp"
#include "../finders/IFinder.hpp"
#include "../finders/desktop/DesktopFinder.hpp"

class CResultButton;

class CUI {
  public:
    CUI(bool open);
    ~CUI();

    void run();
    void setWindowOpen(bool open);
    bool windowOpen();

    // WARNING: has to be called from within the main thread. NOT thread safe!!
    void updateResults(std::vector<SFinderResult>&& results);

    void updateActive();

  private:
    void                                  onSelected();

    SP<Hyprtoolkit::IBackend>             m_backend;
    SP<Hyprtoolkit::IWindow>              m_window;
    SP<Hyprtoolkit::CRectangleElement>    m_background;
    SP<Hyprtoolkit::CColumnLayoutElement> m_layout;

    SP<Hyprtoolkit::CTextboxElement>      m_inputBox;
    SP<Hyprtoolkit::CRectangleElement>    m_hr;
    SP<Hyprtoolkit::CScrollAreaElement>   m_scrollArea;
    SP<Hyprtoolkit::CColumnLayoutElement> m_resultsLayout;

    std::vector<SFinderResult>            m_currentResults;
    std::vector<SP<CResultButton>>        m_resultButtons;

    bool                                  m_open            = false;
    bool                                  m_openByDefault   = true;
    size_t                                m_activeElementId = 0;

    // Submenu state
    std::optional<size_t>                 m_expandedParentIdx;       // index in m_currentResults of expanded parent
    std::vector<SDesktopAction>           m_submenuActions;          // currently visible submenu items (max 10)
    size_t                                m_submenuInsertOffset = 0; // number of submenu items inserted in visible list
    std::string                           m_lastQuery;              // current query for auto-select matching

    // Submenu operations
    void                                  expandSubmenu(size_t parentIdx);
    void                                  collapseSubmenu();
    bool                                  isSubmenuItemIdx(size_t visibleIdx) const;
    size_t                                submenuActionIndex(size_t visibleIdx) const;

    friend class CQueryProcessor;
    friend class CResultButton;
};

inline UP<CUI> g_ui;
