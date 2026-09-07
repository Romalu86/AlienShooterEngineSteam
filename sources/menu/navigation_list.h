#pragma once
#include "../core/types.h"
#include <vector>

namespace as1
{
    class SPRITE;

    namespace menu
    {
        class NavigationList
        {
        public:
            enum class Direction
            {
                Up,
                Down,
                Left,
                Right,
            };

            void clear();
            void createList(const std::vector<SPRITE*>& sprites);
            bool isReachable(SPRITE* sprite) const;
            SPRITE* next(SPRITE* sprite, Direction direction) const;

            void Clear() { clear(); }
            std::size_t Count() const { return m_sprites.size(); }
            const std::vector<SPRITE*>& Sprites() const { return m_sprites; }

        private:
            std::vector<SPRITE*> m_sprites;
        };
    }

    using NAVIGATION_LIST = menu::NavigationList;
}
