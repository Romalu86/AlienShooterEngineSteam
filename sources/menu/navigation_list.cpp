#include "navigation_list.h"
#include <algorithm>

namespace as1
{
    namespace menu
    {
        void NavigationList::clear()
        {
            m_sprites.clear();
        }

        void NavigationList::createList(const std::vector<SPRITE*>& sprites)
        {

            m_sprites = sprites;
        }

        bool NavigationList::isReachable(SPRITE* sprite) const
        {
            return std::find(m_sprites.begin(), m_sprites.end(), sprite) != m_sprites.end();
        }

        SPRITE* NavigationList::next(SPRITE*, Direction) const
        {

            return nullptr;
        }
    }
}
