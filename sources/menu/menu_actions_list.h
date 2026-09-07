#pragma once
#include "../core/types.h"
#include "../core/as_string.h"
#include <vector>

namespace as1
{
    class SPRITE;

    namespace input
    {
        class ControlStates;
    }

    namespace menu
    {
        struct MenuActionBinding
        {
            SPRITE* sprite = nullptr;
            STRING actionName;
        };

        class MenuActionsList
        {
        public:
            void clear();
            void addSprite(SPRITE* sprite, const STRING& actionName);
            void removeSprite(SPRITE* sprite);
            SPRITE* getActivated(const input::ControlStates& controlStates) const;
            std::size_t Count() const { return m_bindings.size(); }
            const std::vector<MenuActionBinding>& Bindings() const { return m_bindings; }

            void Clear() { clear(); }

        private:
            std::vector<MenuActionBinding> m_bindings;
        };
    }

    using MENU_ACTIONS_LIST = menu::MenuActionsList;
}
