#include "ResultButton.hpp"
#include "../finders/IFinder.hpp"

#include "UI.hpp"

CResultButton::CResultButton() {
    const auto FONT_SIZE = Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT}.ptSize();
    m_lastFontSize       = FONT_SIZE;

    const auto BG_HEIGHT = (FONT_SIZE * 2.F) + 4.F;

    m_background = Hyprtoolkit::CRectangleBuilder::begin()
                       ->color([]() {
                           auto c = g_ui->m_backend->getPalette()->m_colors.accent.darken(0.3F);
                           c.a    = 0.F;
                           return c;
                       })
                       ->rounding(g_ui->m_backend->getPalette()->m_vars.smallRounding)
                       ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, BG_HEIGHT}})
                       ->commence();

    m_container =
        Hyprtoolkit::CRowLayoutBuilder::begin()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1, 1}})->gap(4)->commence();
    m_container->setMargin(4);

    m_icon = Hyprtoolkit::CImageBuilder::begin()
                 ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {0.7F * BG_HEIGHT, 0.7F * BG_HEIGHT}})
                 ->commence();
    m_iconPlaceholder = Hyprtoolkit::CNullBuilder::begin()
                            ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {0.7F * BG_HEIGHT, 0.7F * BG_HEIGHT}})
                            ->commence();
    m_icon->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
    m_icon->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);
    m_iconPlaceholder->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
    m_iconPlaceholder->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);

    m_label = Hyprtoolkit::CTextBuilder::begin()
                  ->text(std::string{m_lastLabel})
                  ->align(Hyprtoolkit::HT_FONT_ALIGN_LEFT)
                  ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {1, 1}})
                  ->commence();

    m_label->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
    m_label->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_LEFT, true);
    m_label->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);

    // Create chevron indicator for submenu (initially not added to tree)
    m_submenuChevron = Hyprtoolkit::CTextBuilder::begin()
                           ->text(std::string{"›"})
                           ->a(0.5F)
                           ->align(Hyprtoolkit::HT_FONT_ALIGN_RIGHT)
                           ->size({Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, {20.F, 1.F}})
                           ->commence();
    m_submenuChevron->setPositionMode(Hyprtoolkit::IElement::HT_POSITION_ABSOLUTE);
    m_submenuChevron->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_RIGHT, true);
    m_submenuChevron->setPositionFlag(Hyprtoolkit::IElement::HT_POSITION_FLAG_VCENTER, true);

    m_background->addChild(m_container);
    m_container->addChild(m_label);
}

void CResultButton::setActive(bool active) {
    if (active == m_active)
        return;

    m_active = active;

    m_background->rebuild()
        ->color([this]() {
            auto c = g_ui->m_backend->getPalette()->m_colors.accent.darken(0.3F);
            c.a    = m_active ? 0.4F : 0.F;
            return c;
        })
        ->commence();
}

void CResultButton::setLabel(const std::string& x, const std::string& icon, std::optional<std::string> font, bool canHaveIcon) {

    if (const auto FONT_SIZE = Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT}.ptSize(); FONT_SIZE != m_lastFontSize)
        updatedFontSize();

    if (icon != m_lastIcon) {
        m_lastIcon = canHaveIcon ? icon : "_____NONEXISTENT___ICON_____";

        if (canHaveIcon) {
            auto iconDescription = g_ui->m_backend->systemIcons()->lookupIcon(icon);

            m_container->clearChildren();

            if (!iconDescription || !iconDescription->exists()) {
                m_container->addChild(m_iconPlaceholder);
                m_container->addChild(m_label);
            } else {
                m_icon->rebuild()->icon(iconDescription)->commence();
                m_container->addChild(m_icon);
                m_container->addChild(m_label);
            }
        } else {
            m_container->clearChildren();
            m_container->addChild(m_label);
        }
    }

    if (x != m_lastLabel) {
        m_lastLabel = x;

        m_label->rebuild()->text(std::string{x})->fontFamily(font.value_or(g_ui->m_backend->getPalette()->m_vars.fontFamily))->commence();
    }
}

void CResultButton::updatedFontSize() {
    if (!m_background)
        return;

    const auto FONT_SIZE = Hyprtoolkit::CFontSize{Hyprtoolkit::CFontSize::HT_FONT_TEXT}.ptSize();

    m_background->rebuild()->size({Hyprtoolkit::CDynamicSize::HT_SIZE_PERCENT, Hyprtoolkit::CDynamicSize::HT_SIZE_ABSOLUTE, {1.F, (FONT_SIZE * 2.F) + 4.F}})->commence();

    m_lastFontSize = FONT_SIZE;
}

void CResultButton::setSubmenuIndicator(bool show) {
    if (show == m_hasSubmenuIndicator)
        return;
    m_hasSubmenuIndicator = show;
    if (show) {
        m_background->addChild(m_submenuChevron);
    } else {
        m_background->removeChild(m_submenuChevron);
    }
}

void CResultButton::setIndented(bool indented) {
    if (indented == m_indented)
        return;
    m_indented = indented;
    // Apply larger uniform margin to visually indent submenu items.
    // Since setMargin applies to all sides, the increased value creates
    // a visible left offset within the row layout.
    m_container->setMargin(indented ? 20 : 4);
}

void CResultButton::setSubmenuStyle(bool isSubmenuItem) {
    if (isSubmenuItem == m_isSubmenuItem)
        return;
    m_isSubmenuItem = isSubmenuItem;
    // Apply reduced opacity to the label text for submenu items
    if (m_label) {
        m_label->rebuild()
            ->color([this]() -> Hyprtoolkit::CHyprColor {
                auto c = g_ui->m_backend->getPalette()->m_colors.text;
                if (m_isSubmenuItem)
                    c.a = 0.7; // slightly dimmer for submenu items
                return c;
            })
            ->text(std::string{m_lastLabel})
            ->fontFamily(std::string{g_ui->m_backend->getPalette()->m_vars.fontFamily})
            ->commence();
    }
}
