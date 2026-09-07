#include "menu.h"

#include <cmath>
#include <new>

#include "core/application.h"
#include "core/log.h"
#include "core/resource.h"
#include "graph.h"
#include "input.h"
#include "map.h"
#include "sprite.h"
#include "vid/vid.h"

namespace as1
{
    namespace
    {
        class MenuVtableOwner final
        {
        public:
            virtual void* deletingDestructor(unsigned char flags) noexcept
            {
                return reinterpret_cast<MENU*>(this)->menuDeletingDestructor(flags);
            }
        };

        std::uint32_t currentImageMenuVtable() noexcept
        {
#if UINTPTR_MAX == 0xFFFFFFFFu
            static MenuVtableOwner owner;
            return static_cast<std::uint32_t>(*reinterpret_cast<const std::uintptr_t*>(&owner));
#else
            return 0u;
#endif
        }
    }

    std::uint32_t MENU::CurrentImageMenuVtable() noexcept
    {
        return currentImageMenuVtable();
    }

    MENU::MENU() noexcept
    {
        initializeMenuRecord();
    }

    MENU::~MENU()
    {
        destroyMenuRecord(false);
    }

    void MENU::initializeMenuRecord() noexcept
    {
        m_controlFlags &= ~0x3u;
        m_selectedSprite = nullptr;
        setRetailVtableToken(CurrentImageMenuVtable());
    }

    MENU* MENU::destroyMenuRecord(bool deleteSelfFlag) noexcept
    {
        setRetailVtableToken(SPRITE_POINTER_LIST::CurrentImageCoreListVtable());
        ::operator delete(m_items);
        m_items = nullptr;
        m_count = 0;
        if (deleteSelfFlag)
            ::operator delete(static_cast<void*>(this));
        return this;
    }

    void* MENU::menuDeletingDestructor(unsigned char deletingDestructorFlags) noexcept
    {
        return destroyMenuRecord((deletingDestructorFlags & 1u) != 0u);
    }

    int MENU::Load(const STRING& path, int options)
    {

        RESOURCE resource;
        if (!resource.openFile(path, RESOURCE::ResTypes::MENU))
        {
            LOG::ResourceError("MENU", 7, path.c_str(), 0, 0);
            return 1;
        }

        if (resource.GoBegin(RESOURCE::ResTypes::HEAD) != 0)
        {
            LOG::ResourceError("MENU", 11, "'HEAD'in menu", 0, 0);
            return 1;
        }

        int version = 0;
        int sizeX = 0;
        int sizeY = 0;
        int shiftX = 0;
        int shiftY = 0;
        resource.read(&version, 4);
        resource.read(&sizeX, 4);
        resource.read(&sizeY, 4);
        resource.read(&shiftX, 4);
        resource.read(&shiftY, 4);

        MAP* const map = MAP::Current();
        GRAPH* const graph = GRAPH::CurrentGraph();
        const auto& drawState = core::GlobalApplicationDrawDispatcherState();

        const auto centerSprite = [&](SPRITE* sprite, bool allowHudAnchoring)
        {
            if (!sprite)
                return;

            if (allowHudAnchoring && (options & 2) != 0 && graph)
            {
                const float z = sprite->Z();
                const float cameraShiftX = drawState.cameraShiftX();
                const float cameraShiftY = drawState.cameraShiftY();
                const float authoredX = sprite->X() - cameraShiftX;
                const float authoredY = (sprite->Y() - z) - cameraShiftY;
                const float integerShiftX = static_cast<float>(static_cast<int>(cameraShiftX));
                const float integerShiftY = static_cast<float>(static_cast<int>(cameraShiftY));
                const float viewportLeft = static_cast<float>(graph->getViewportLeft());
                const float viewportTop = static_cast<float>(graph->getViewportTop());
                const float viewportBottom = static_cast<float>(graph->getViewportBottom());

                float x = authoredX + integerShiftX;
                float y = viewportTop + authoredY + integerShiftY + z;
                if (authoredX <= 924.0f)
                {
                    if (authoredY <= 668.0f)
                    {
                        if (authoredX < 100.0f)
                            x = viewportLeft + authoredX + integerShiftX;
                    }
                    else
                    {
                        x = viewportLeft + authoredX + integerShiftX;
                        y = viewportBottom - (768.0f - authoredY) + integerShiftY + z;
                    }
                }
                sprite->ChangeCoor(x, y, z);
                return;
            }

            const float x = sprite->X()
                - static_cast<float>(shiftX)
                - static_cast<float>(sizeX / 2)
                + static_cast<float>(graph->SizeX()) * 0.5f;
            const float y = sprite->Y()
                - static_cast<float>(shiftY)
                - static_cast<float>(sizeY / 2)
                + static_cast<float>(graph->SizeY()) * 0.5f;
            sprite->ChangeCoor(x, y, sprite->Z());
        };

        SPRITE* const endSprite = reinterpret_cast<SPRITE*>(~static_cast<std::uintptr_t>(0));

        if (resource.GoNext(RESOURCE::ResTypes::SPRITE) == 0)
        {
            for (;;)
            {
                SPRITE* const sprite = map->LoadSprite(&resource, version);
                if (sprite == endSprite)
                    break;
                if (sprite)
                {
                    centerSprite(sprite, true);
#if UINTPTR_MAX <= UINT32_MAX
                    sprite->dispatchVirtualAction(ActionCode::ACT_RESTORE,
                        static_cast<int>(reinterpret_cast<std::uintptr_t>(&resource)),
                        version,
                        0);
#endif
                }

                if (resource.GoNextSub(RESOURCE::ResTypes::SPRITE) != 0)
                    break;
            }
            return 0;
        }

        if (resource.GoBegin(RESOURCE::ResTypes::SPRI) == 0)
        {
            for (;;)
            {
                SPRITE* const sprite = map->LoadSprite(&resource, version);
                if (sprite == endSprite)
                    break;
                centerSprite(sprite, false);
            }
            return 0;
        }

        LOG::ResourceError("MENU", 11, "'SPR ' or 'SPRI' in menu", 0, 0);
        return 1;
    }

    int MENU::DeleteFromFile(const STRING& path)
    {
        RESOURCE resource;
        if (!resource.openFile(path, RESOURCE::ResTypes::MENU))
        {
            LOG::ResourceError("MENU", 7, path.c_str(), 0, 0);
            return 1;
        }

        if (resource.GoBegin(RESOURCE::ResTypes::HEAD) != 0)
        {
            LOG::ResourceError("MENU", 11, "'HEAD'in menu", 0, 0);
            return 1;
        }

        int version = 0;
        int sizeX = 0;
        int sizeY = 0;
        int shiftX = 0;
        int shiftY = 0;
        resource.read(&version, 4);
        resource.read(&sizeX, 4);
        resource.read(&sizeY, 4);
        resource.read(&shiftX, 4);
        resource.read(&shiftY, 4);
        (void)version;

        GRAPH* const graph = GRAPH::CurrentGraph();
        if (!graph)
            return 1;

        if (resource.GoNext(RESOURCE::ResTypes::SPRITE) != 0)
        {
            LOG::ResourceError("MENU", 11, "'SPR ' in MENU::DeleteFromFile", 0, 0);
            return 1;
        }

        constexpr float kRetailPositionEpsilon = 0.001f;
        for (;;)
        {
            int oldAddress = 0;
            resource.read(&oldAddress, 4);
            if (oldAddress == -1)
                break;

            int nvid = -1;
            float authoredX = 0.0f;
            float authoredY = 0.0f;
            float authoredZ = 0.0f;
            resource.read(&nvid, 4);
            resource.read(&authoredX, 4);
            resource.read(&authoredY, 4);
            resource.read(&authoredZ, 4);

            const float expectedX = authoredX
                - static_cast<float>(shiftX)
                - static_cast<float>(sizeX / 2)
                + static_cast<float>(graph->SizeX()) * 0.5f;
            const float expectedY = authoredY
                - static_cast<float>(shiftY)
                - static_cast<float>(sizeY / 2)
                + static_cast<float>(graph->SizeY()) * 0.5f;

            for (int i = 0; i < m_count; ++i)
            {
                SPRITE* const sprite = m_items[i];
                VID* const vid = sprite ? sprite->Vid() : nullptr;
                if (!sprite || !vid || vid->nvid() != nvid)
                    continue;

                if (std::fabs(sprite->X() - expectedX) >= kRetailPositionEpsilon ||
                    std::fabs(sprite->Y() - expectedY) >= kRetailPositionEpsilon ||
                    std::fabs(sprite->Z() - authoredZ) >= kRetailPositionEpsilon)
                    continue;

                (void)releaseByIndexRetail(i);
                --i;
            }

            if (resource.GoNextSub(RESOURCE::ResTypes::SPRITE) != 0)
                break;
        }
        return 0;
    }

    int MENU::Control(input::InputMessageState* input)
    {
        m_selectedSprite = nullptr;
        m_controlFlags &= ~0x3u;

        for (int index = 0; index < m_count; ++index)
        {
            SPRITE* const sprite = m_items[index];
            if (!sprite)
                continue;

            VID* const vid = sprite->Vid();
            if (vid->movementMask() == 0u)
                continue;

            const int animation = sprite->currentAnimation();
            if (animation == 14 || animation >= 15 || animation == 7 || animation == 6)
                continue;

            if (!sprite->IsInsideRetail(input->worldX, input->worldY) ||
                (m_selectedSprite && sprite->Z() <= m_selectedSprite->Z()))
            {
                sprite->ChangeAnimation(animation & 1);
                continue;
            }

            m_selectedSprite = sprite;
        }

        if (!m_selectedSprite)
            return 0;

        if ((input->flags & 0x1u) != 0u)
        {
            m_controlFlags |= 0x1u;
            (void)input->clearLeftButtonState();
            m_selectedSprite->ChangeAnimation((m_selectedSprite->currentAnimation() & 1) | 4);
            return 1;
        }

        if ((input->flags & 0x4u) != 0u)
        {
            m_controlFlags |= 0x2u;
            (void)input->clearRightButtonState();
        }

        const int animation = m_selectedSprite->currentAnimation();
        if ((animation & ~1) != 4)
            m_selectedSprite->ChangeAnimation((animation & 1) | 2);
        return 0;
    }

    int MENU::SelectedNvid() const noexcept
    {

        const SPRITE* selected = m_selectedSprite;
        if (!selected)
            return 0;

        const VID* vid = selected->Vid();
        return vid->nvid();
    }

    int MENU::SelectedDirectionFrame() const noexcept
    {

        const SPRITE* selected = m_selectedSprite;
        if (!selected)
            return 0;

        const VID* vid = selected->Vid();

        const std::uint32_t directionByte =
            static_cast<std::uint32_t>(vid->directionQuantizationOffset() +
                                       selected->directionIndex()) & 0xFFu;
        const std::uint32_t noDir = static_cast<std::uint32_t>(vid->directionCount());
        return static_cast<int>((directionByte * noDir) >> 8);
    }


}
