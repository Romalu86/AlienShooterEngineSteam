#include "3rdparty/win/libvorbis/lib/backends.h"

namespace as1 { namespace thirdparty { namespace xiph2003
{
    namespace
    {
        constexpr FloorBackendRoute kFloorBackends[] = {
            {0, parseFloor0SetupRecord, releaseFloor0Record},
            {1, parseFloor1SetupRecord, releaseFloor1Record},
        };

        constexpr ResidueBackendRoute kResidueBackends[] = {
            {0, parseResidueSetupRecord, releaseResidueRecord},
            {1, parseResidueSetupRecord, releaseResidueRecord},
            {2, parseResidueSetupRecord, releaseResidueRecord},
        };

        constexpr MappingBackendRoute kMappingBackends[] = {
            {0, parseMapping0SetupRecord, releaseMapping0Record},
        };

        template <typename T, std::size_t N>
        const T* findBackendByType(const T (&table)[N], int type)
        {
            for (const T& entry : table)
            {
                if (entry.type == type)
                    return &entry;
            }
            return nullptr;
        }
    }

    void* parseFloorSetupRecordByType(int type, void* infoRecord, BitCursor& cursor)
    {
        const FloorBackendRoute* backend = findBackendByType(kFloorBackends, type);
        return backend ? backend->unpack(infoRecord, cursor) : nullptr;
    }

    void* parseResidueSetupRecordByType(int type, void* infoRecord, BitCursor& cursor)
    {
        const ResidueBackendRoute* backend = findBackendByType(kResidueBackends, type);
        return backend ? backend->unpack(infoRecord, cursor) : nullptr;
    }

    void* parseMappingSetupRecordByType(int type, void* infoRecord, BitCursor& cursor)
    {
        const MappingBackendRoute* backend = findBackendByType(kMappingBackends, type);
        return backend ? backend->unpack(infoRecord, cursor) : nullptr;
    }

    void releaseFloorRecordByType(int type, void* record)
    {
        if (!record)
            return;
        const FloorBackendRoute* backend = findBackendByType(kFloorBackends, type);
        if (backend)
            backend->release(record);
    }

    void releaseResidueRecordByType(int type, void* record)
    {
        if (!record)
            return;
        const ResidueBackendRoute* backend = findBackendByType(kResidueBackends, type);
        if (backend)
            backend->release(record);
    }

    void releaseMappingRecordByType(int type, void* record)
    {
        if (!record)
            return;
        const MappingBackendRoute* backend = findBackendByType(kMappingBackends, type);
        if (backend)
            backend->release(record);
    }
} } }
