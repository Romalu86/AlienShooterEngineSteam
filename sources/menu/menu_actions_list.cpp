#include "menu_actions_list.h"
#include <algorithm>

namespace as1
{
    namespace menu
    {
        void MenuActionsList::clear()
        {
            m_bindings.clear();
        }

        void MenuActionsList::addSprite(SPRITE* sprite, const STRING& actionName)
        {
            if (!sprite)
                return;
            m_bindings.push_back({sprite, actionName});
        }

        void MenuActionsList::removeSprite(SPRITE* sprite)
        {
            m_bindings.erase(
                std::remove_if(m_bindings.begin(), m_bindings.end(), [sprite](const MenuActionBinding& entry) { return entry.sprite == sprite; }),
                m_bindings.end());
        }

        SPRITE* MenuActionsList::getActivated(const input::ControlStates&) const
        {

            return nullptr;
        }
    }
}
