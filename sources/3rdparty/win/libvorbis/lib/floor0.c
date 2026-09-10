#include "3rdparty/win/libvorbis/lib/codec_internal.h"

namespace as1 { namespace thirdparty { namespace xiph2003
{
        static float floorLookupGainTable[256] =
        {
            1.06498632e-07f, 1.1341951e-07f, 1.20790148e-07f, 1.28639783e-07f, 1.36999503e-07f, 1.45902504e-07f, 1.55384086e-07f, 1.65481808e-07f,
            1.76235744e-07f, 1.87688556e-07f, 1.99885605e-07f, 2.12875307e-07f, 2.26709133e-07f, 2.41441967e-07f, 2.57132228e-07f, 2.73842119e-07f,
            2.91637917e-07f, 3.10590224e-07f, 3.307741e-07f, 3.52269666e-07f, 3.75162131e-07f, 3.99542301e-07f, 4.25506812e-07f, 4.53158634e-07f,
            4.82607447e-07f, 5.13970008e-07f, 5.47370632e-07f, 5.8294188e-07f, 6.20824721e-07f, 6.61169395e-07f, 7.04135914e-07f, 7.49894639e-07f,
            7.98627013e-07f, 8.50526305e-07f, 9.05798288e-07f, 9.64662149e-07f, 1.02735135e-06f, 1.0941144e-06f, 1.16521608e-06f, 1.24093845e-06f,
            1.32158164e-06f, 1.40746545e-06f, 1.49893049e-06f, 1.59633942e-06f, 1.70007854e-06f, 1.81055918e-06f, 1.92821949e-06f, 2.05352603e-06f,
            2.18697573e-06f, 2.3290977e-06f, 2.48045581e-06f, 2.64164964e-06f, 2.81331904e-06f, 2.9961443e-06f, 3.19085052e-06f, 3.39821008e-06f,
            3.61904495e-06f, 3.85423073e-06f, 4.10470057e-06f, 4.37144718e-06f, 4.6555283e-06f, 4.9580708e-06f, 5.28027385e-06f, 5.6234162e-06f,
            5.98885708e-06f, 6.37804669e-06f, 6.79252844e-06f, 7.23394533e-06f, 7.70404768e-06f, 8.20469995e-06f, 8.73788758e-06f, 9.30572514e-06f,
            9.91046363e-06f, 1.05545014e-05f, 1.12403923e-05f, 1.19708557e-05f, 1.27487892e-05f, 1.3577278e-05f, 1.44596061e-05f, 1.53992714e-05f,
            1.64000048e-05f, 1.74657689e-05f, 1.86007928e-05f, 1.98095768e-05f, 2.10969138e-05f, 2.24679115e-05f, 2.39280016e-05f, 2.54829774e-05f,
            2.71390054e-05f, 2.89026502e-05f, 3.07809096e-05f, 3.27812268e-05f, 3.49115326e-05f, 3.71802817e-05f, 3.95964671e-05f, 4.21696677e-05f,
            4.49100917e-05f, 4.7828602e-05f, 5.09367746e-05f, 5.42469315e-05f, 5.77722021e-05f, 6.15265672e-05f, 6.55249096e-05f, 6.97830837e-05f,
            7.43179844e-05f, 7.91475832e-05f, 8.42910376e-05f, 8.97687496e-05f, 9.56024232e-05f, 0.000101815211f, 0.000108431741f, 0.000115478237f,
            0.000122982674f, 0.000130974775f, 0.000139486248f, 0.000148550855f, 0.000158204537f, 0.000168485552f, 0.00017943469f, 0.000191095358f,
            0.000203513817f, 0.000216739296f, 0.000230824226f, 0.000245824485f, 0.000261799549f, 0.000278812746f, 0.000296931568f, 0.000316227874f,
            0.000336778146f, 0.000358663878f, 0.000381971884f, 0.00040679457f, 0.000433230365f, 0.000461384101f, 0.000491367478f, 0.00052329927f,
            0.000557306223f, 0.000593523087f, 0.000632093579f, 0.000673170609f, 0.000716916984f, 0.000763506279f, 0.000813123246f, 0.000865964568f,
            0.000922239851f, 0.000982172205f, 0.00104599923f, 0.00111397426f, 0.00118636654f, 0.00126346329f, 0.0013455702f, 0.00143301289f,
            0.00152613816f, 0.00162531529f, 0.00173093739f, 0.00184342347f, 0.00196321961f, 0.00209080055f, 0.0022266726f, 0.00237137428f,
            0.00252547953f, 0.00268959929f, 0.00286438479f, 0.0030505287f, 0.00324876909f, 0.00345989247f, 0.00368473586f, 0.00392419053f,
            0.00417920668f, 0.00445079478f, 0.00474003283f, 0.00504806684f, 0.0053761187f, 0.005725489f, 0.00609756354f, 0.00649381755f,
            0.00691582263f, 0.00736525143f, 0.00784388743f, 0.00835362729f, 0.00889649242f, 0.00947463699f, 0.010090352f, 0.0107460804f,
            0.0114444206f, 0.012188144f, 0.0129801976f, 0.0138237253f, 0.0147220679f, 0.0156787913f, 0.0166976862f, 0.0177827962f,
            0.0189384222f, 0.0201691482f, 0.0214798544f, 0.0228757355f, 0.0243623294f, 0.0259455312f, 0.0276316181f, 0.0294272769f,
            0.0313396268f, 0.0333762504f, 0.0355452262f, 0.0378551558f, 0.0403151996f, 0.0429351069f, 0.0457252748f, 0.0486967564f,
            0.0518613495f, 0.0552315898f, 0.0588208511f, 0.0626433641f, 0.0667142794f, 0.0710497499f, 0.0756669641f, 0.080584228f,
            0.0858210474f, 0.0913981795f, 0.0973377451f, 0.103663303f, 0.110399932f, 0.117574342f, 0.125214979f, 0.133352146f,
            0.142018124f, 0.151247263f, 0.161076173f, 0.171543807f, 0.182691678f, 0.194564015f, 0.207207873f, 0.220673427f,
            0.235014021f, 0.250286549f, 0.266551584f, 0.283873618f, 0.302321315f, 0.32196787f, 0.342891127f, 0.365174145f,
            0.388905197f, 0.414178461f, 0.44109413f, 0.469758898f, 0.50028646f, 0.532797933f, 0.567422092f, 0.604296386f,
            0.643566966f, 0.685389578f, 0.729930043f, 0.777365029f, 0.827882588f, 0.881683052f, 0.938979805f, 1.0f
        };

        static float cosineLookupTable[129] =
        {
            1.0f, 0.999698818f, 0.99879545f, 0.997290432f, 0.99518472f, 0.992479563f, 0.989176512f, 0.985277653f,
            0.980785251f, 0.975702107f, 0.970031261f, 0.963776052f, 0.956940353f, 0.949528158f, 0.941544056f, 0.932992816f,
            0.923879504f, 0.914209783f, 0.903989315f, 0.893224299f, 0.881921291f, 0.870086968f, 0.857728601f, 0.84485358f,
            0.831469595f, 0.817584813f, 0.803207517f, 0.78834641f, 0.773010433f, 0.757208824f, 0.740951121f, 0.724247098f,
            0.707106769f, 0.689540565f, 0.671558976f, 0.653172851f, 0.634393275f, 0.615231574f, 0.59569931f, 0.575808167f,
            0.555570245f, 0.534997642f, 0.514102757f, 0.492898196f, 0.471396744f, 0.449611336f, 0.427555084f, 0.405241311f,
            0.382683426f, 0.359895051f, 0.336889863f, 0.313681751f, 0.290284663f, 0.266712755f, 0.242980182f, 0.219101235f,
            0.195090324f, 0.170961887f, 0.146730468f, 0.122410677f, 0.0980171412f, 0.0735645667f, 0.0490676761f, 0.024541229f,
            0.0f, -0.024541229f, -0.0490676761f, -0.0735645667f, -0.0980171412f, -0.122410677f, -0.146730468f, -0.170961887f,
            -0.195090324f, -0.219101235f, -0.242980182f, -0.266712755f, -0.290284663f, -0.313681751f, -0.336889863f, -0.359895051f,
            -0.382683426f, -0.405241311f, -0.427555084f, -0.449611336f, -0.471396744f, -0.492898196f, -0.514102757f, -0.534997642f,
            -0.555570245f, -0.575808167f, -0.59569931f, -0.615231574f, -0.634393275f, -0.653172851f, -0.671558976f, -0.689540565f,
            -0.707106769f, -0.724247098f, -0.740951121f, -0.757208824f, -0.773010433f, -0.78834641f, -0.803207517f, -0.817584813f,
            -0.831469595f, -0.84485358f, -0.857728601f, -0.870086968f, -0.881921291f, -0.893224299f, -0.903989315f, -0.914209783f,
            -0.923879504f, -0.932992816f, -0.941544056f, -0.949528158f, -0.956940353f, -0.963776052f, -0.970031261f, -0.975702107f,
            -0.980785251f, -0.985277653f, -0.989176512f, -0.992479563f, -0.99518472f, -0.997290432f, -0.99879545f, -0.999698818f,
            -1.0f
        };

        static float inverseSqrtLookupTable[33] =
        {
            1.41421354f, 1.39262128f, 1.37198865f, 1.35224676f, 1.33333337f, 1.31519186f, 1.29777133f, 1.28102517f,
            1.26491106f, 1.24939013f, 1.23442686f, 1.21998858f, 1.20604539f, 1.19256961f, 1.17953563f, 1.16691995f,
            1.15470052f, 1.14285719f, 1.1313709f, 1.12022412f, 1.10940039f, 1.09888446f, 1.08866215f, 1.07871974f,
            1.06904495f, 1.05962586f, 1.05045152f, 1.0415113f, 1.03279555f, 1.02429509f, 1.01600099f, 1.00790524f,
            1.0f
        };

        static float inverseSqrtExponentTable[33] =
        {
            1.0f, 0.707106769f, 0.5f, 0.353553385f, 0.25f, 0.176776692f, 0.125f, 0.0883883461f,
            0.0625f, 0.0441941731f, 0.03125f, 0.0220970865f, 0.015625f, 0.0110485433f, 0.0078125f, 0.00552427163f,
            0.00390625f, 0.00276213582f, 0.001953125f, 0.00138106791f, 0.0009765625f, 0.000690533954f, 0.00048828125f, 0.000345266977f,
            0.000244140625f, 0.000172633489f, 0.000122070312f, 8.63167443e-05f, 6.10351562e-05f, 4.31583721e-05f, 3.05175781e-05f, 2.15791861e-05f,
            1.52587891e-05f
        };

        static float fromDbCoarseLookupTable[35] =
        {
            1.0f, 0.630957365f, 0.398107171f, 0.251188636f, 0.158489317f, 0.100000001f, 0.0630957335f, 0.0398107171f,
            0.0251188651f, 0.0158489328f, 0.00999999978f, 0.00630957354f, 0.00398107152f, 0.00251188641f, 0.00158489321f, 0.00100000005f,
            0.000630957366f, 0.000398107164f, 0.000251188641f, 0.000158489318f, 9.99999975e-05f, 6.30957366e-05f, 3.98107186e-05f, 2.51188649e-05f,
            1.58489311e-05f, 9.99999975e-06f, 6.30957356e-06f, 3.98107159e-06f, 2.51188635e-06f, 1.5848932e-06f, 9.99999997e-07f, 6.30957345e-07f,
            3.98107176e-07f, 2.51188652e-07f, 1.58489314e-07f
        };

        static float fromDbFineLookupTable[32] =
        {
            0.992830276f, 0.978644609f, 0.964661598f, 0.950878441f, 0.937292218f, 0.923900068f, 0.910699308f, 0.897687137f,
            0.884860873f, 0.872217894f, 0.859755576f, 0.847471297f, 0.835362554f, 0.823426783f, 0.811661601f, 0.800064504f,
            0.788633108f, 0.777365029f, 0.766257942f, 0.755309582f, 0.744517624f, 0.733879924f, 0.723394156f, 0.713058233f,
            0.702870011f, 0.692827284f, 0.682928145f, 0.673170388f, 0.663552046f, 0.654071152f, 0.64472574f, 0.635513842f
        };

        void releaseFloor0Record(void* record)
        {
            if (!record)
                return;
            std::memset(record, 0, 0x60);
            std::free(record);
        }

        void* parseFloor0SetupRecord(void* infoRecord, BitCursor& cursor)
        {
            if (!infoRecord)
                return nullptr;
            auto* info = static_cast<unsigned char*>(infoRecord);
            auto* setup = static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));
            if (!setup)
                return nullptr;

            auto* floor = static_cast<unsigned char*>(std::malloc(0x60));
            if (!floor)
                return nullptr;
            std::memset(floor, 0, 0x60);

            writeBlockField<int>(floor, 0x00, readBits(cursor, 8));
            writeBlockField<int>(floor, 0x04, readBits(cursor, 16));
            writeBlockField<int>(floor, 0x08, readBits(cursor, 16));
            writeBlockField<int>(floor, 0x0C, readBits(cursor, 6));
            writeBlockField<int>(floor, 0x10, readBits(cursor, 8));
            const int bookCount = readBits(cursor, 4) + 1;
            writeBlockField<int>(floor, 0x14, bookCount);

            if (readBlockField<int>(floor, 0x00) < 1
                || readBlockField<int>(floor, 0x04) < 1
                || readBlockField<int>(floor, 0x08) < 1
                || bookCount < 1)
            {
                releaseFloor0Record(floor);
                return nullptr;
            }

            for (int index = 0; index < bookCount; ++index)
            {
                const int book = readBits(cursor, 8);
                writeBlockField<int>(floor, 0x18 + 4 * static_cast<std::size_t>(index), book);
                if (book < 0 || book >= readBlockField<int>(setup, kSetupBookCountOffset))
                {
                    releaseFloor0Record(floor);
                    return nullptr;
                }
            }
            return floor;
        }


        void* buildFloor0LookRecord(const unsigned char* setup, int floorIndex)
        {
            if (!setup || floorIndex < 0 || floorIndex >= readBlockField<int>(setup, kSetupFloorCountOffset))
                return nullptr;
            if (readBlockField<int>(setup, kSetupFloorTypeTableOffset + 4 * static_cast<std::size_t>(floorIndex)) != 0)
                return nullptr;
            auto* info = static_cast<unsigned char*>(readBlockPointer(setup, kSetupFloorPointerTableOffset + 4 * static_cast<std::size_t>(floorIndex)));
            if (!info)
                return nullptr;

            auto* look = static_cast<unsigned char*>(std::calloc(1, 0x38));
            if (!look)
                return nullptr;
            const int order = readBlockField<int>(info, 0x00);
            const int barkMap = readBlockField<int>(info, 0x08);
            writeBlockField<int>(look, 0x00, barkMap);
            writeBlockField<int>(look, 0x04, order);
            writeBlockPointer(look, 0x14, info);

            auto* linearmaps = static_cast<unsigned char*>(std::calloc(2, 4));
            auto* cosine = barkMap > 0 ? static_cast<float*>(std::malloc(4 * static_cast<std::size_t>(barkMap))) : nullptr;
            writeBlockPointer(look, 0x08, linearmaps);
            writeBlockPointer(look, 0x2C, cosine);
            if (!linearmaps || (barkMap > 0 && !cosine))
            {
                releaseFloor0LookRecord(look);
                return nullptr;
            }

            if (cosine)
            {
                const double step = 3.14159265358979323846 / static_cast<double>(barkMap);
#if defined(_MSC_VER) && defined(_M_IX86)
#pragma loop(no_vector)
#endif
                for (int index = 0; index < barkMap; ++index)
                    cosine[index] = static_cast<float>(2.0 * std::cos(step * static_cast<double>(index)));
            }
            return look;
        }

        void releaseFloor0LookRecord(void* record)
        {
            if (!record)
                return;
            auto* look = static_cast<unsigned char*>(record);
            if (auto* linearmaps = static_cast<unsigned char*>(readBlockPointer(look, 0x08)))
            {
                if (void* map0 = readBlockPointer(linearmaps, 0x00))
                    std::free(map0);
                if (void* map1 = readBlockPointer(linearmaps, 0x04))
                    std::free(map1);
                std::free(linearmaps);
            }
            if (void* cosine = readBlockPointer(look, 0x2C))
                std::free(cosine);
            std::memset(look, 0, 0x38);
            std::free(look);
        }

        static double floor0Bark(double frequency);

        int ensureFloor0LinearMap(unsigned char* block, unsigned char* floorInfo, unsigned char* floorLook)
        {
            const int W = readBlockField<int>(block, kBlockLongModeOffset);
            auto* linearmaps = static_cast<unsigned char*>(readBlockPointer(floorLook, 0x08));
            if (!readBlockPointer(linearmaps, 4 * static_cast<std::size_t>(W)))
            {
                auto* synthesisState = static_cast<unsigned char*>(readBlockPointer(block, kBlockQueueOwnerOffset));
                auto* info = static_cast<unsigned char*>(readBlockPointer(synthesisState, kDecodeStateOwnerOffset));
                auto* setup = static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset));
                const int n = readBlockField<int>(setup, 4 * static_cast<std::size_t>(W)) / 2;
                const int rate = readBlockField<int>(floorInfo, 0x04);
                const double nyquist = static_cast<double>(rate) * 0.5;
                const float scale = static_cast<float>(static_cast<double>(readBlockField<int>(floorLook, 0x00)) / floor0Bark(nyquist));
                auto* map = static_cast<int*>(std::malloc(4 * static_cast<std::size_t>(n) + 4));
                writeBlockPointer(linearmaps, 4 * static_cast<std::size_t>(W), map);
                for (int i = 0; i < n; ++i)
                {
                    const double frequency = nyquist / static_cast<double>(n) * static_cast<double>(i);
                    int value = static_cast<int>(std::floor(floor0Bark(frequency) * static_cast<double>(scale)));
                    if (value >= readBlockField<int>(floorLook, 0x00))
                        value = readBlockField<int>(floorLook, 0x00) - 1;
                    map[i] = value;
                }
                map[n] = -1;
                writeBlockField<int>(floorLook, 0x0C + 4 * static_cast<std::size_t>(W), n);
            }
            return W;
        }

        void* decodeFloor0Memo(unsigned char* block, unsigned char* floorLook)
        {
            if (!block || !floorLook)
                return nullptr;
            auto* floorInfo = static_cast<unsigned char*>(readBlockPointer(floorLook, 0x14));
            auto* synthesisState = static_cast<unsigned char*>(readBlockPointer(block, kBlockQueueOwnerOffset));
            auto* info = synthesisState ? static_cast<unsigned char*>(readBlockPointer(synthesisState, kDecodeStateOwnerOffset)) : nullptr;
            auto* setup = info ? static_cast<unsigned char*>(readBlockPointer(info, kInfoSetupPointerOffset)) : nullptr;
            if (!floorInfo || !setup)
                return nullptr;

            auto& cursor = *reinterpret_cast<BitCursor*>(block + 0x04);
            const int amplitudeBits = readBlockField<int>(floorInfo, 0x0C);
            const int amplitudeRaw = readBits(cursor, amplitudeBits);
            if (amplitudeRaw <= 0)
                return nullptr;

            const int maxAmplitude = (1 << amplitudeBits) - 1;
            const float amplitude = static_cast<float>(amplitudeRaw)
                / static_cast<float>(maxAmplitude)
                * static_cast<float>(readBlockField<int>(floorInfo, 0x10));

            const int bookCount = readBlockField<int>(floorInfo, 0x14);
            // ilog(bookCount), not ilog(bookCount - 1).  This distinction is
            // live for the retail menu_mus.ogg: both floor0 records have two
            // books, so the original consumes two selector bits.
            const int bookBits = countBitsForUnsignedValue(static_cast<unsigned int>(bookCount));
            const int selectedBook = readBits(cursor, bookBits);
            if (selectedBook < 0 || selectedBook >= bookCount)
                return nullptr;

            const int bookIndex = readBlockField<int>(floorInfo, 0x18 + 4 * static_cast<std::size_t>(selectedBook));
            const auto* runtimeBook = codebookRecordFromSetup(setup, bookIndex);
            if (!runtimeBook)
                return nullptr;
            const int dimension = readBlockField<int>(runtimeBook, kRuntimeBookDimensionsOffset);
            const int order = readBlockField<int>(floorLook, 0x04);
            if (dimension <= 0 || order <= 0)
                return nullptr;

            auto* lsp = static_cast<float*>(allocateBlockScratch(block, 4 * (order + dimension) + 4));
            if (!lsp)
                return nullptr;
            int decoded = 0;
            while (decoded < order)
            {
                if (decodeCodebookVectorsSet(runtimeBook, cursor, lsp + decoded, dimension) == -1)
                    return nullptr;
                decoded += dimension;
            }

            float last = 0.0f;
            for (int j = 0; j < order; )
            {
                int k = 0;
                for (; k < dimension && j < order; ++k, ++j)
                    lsp[j] += last;
                if (j > 0)
                    last = lsp[j - 1];
            }
            lsp[order] = amplitude;
            return lsp;
        }

        int applyFloor0Memo(unsigned char* block, unsigned char* floorLook, void* memo, float* samples)
        {
            if (!block || !floorLook || !samples)
                return 0;
            auto* floorInfo = static_cast<unsigned char*>(readBlockPointer(floorLook, 0x14));
            if (!floorInfo)
                return 0;
            ensureFloor0LinearMap(block, floorInfo, floorLook);
            const int W = readBlockField<int>(block, kBlockLongModeOffset);
            auto* linearmaps = static_cast<unsigned char*>(readBlockPointer(floorLook, 0x08));
            auto* map = linearmaps ? static_cast<int*>(readBlockPointer(linearmaps, 4 * static_cast<std::size_t>(W))) : nullptr;
            const int n = readBlockField<int>(floorLook, 0x0C + 4 * static_cast<std::size_t>(W));
            if (memo)
            {
                auto* lsp = static_cast<float*>(memo);
                const int order = readBlockField<int>(floorLook, 0x04);
                if (map)
                {
                    renderFloor0LspCurve(
                        samples,
                        map,
                        n,
                        readBlockField<int>(floorLook, 0x00),
                        lsp,
                        order,
                        lsp[order],
                        static_cast<float>(readBlockField<int>(floorInfo, 0x10)));
                }
                return 1;
            }
            if (n > 0)
                std::memset(samples, 0, 4 * static_cast<std::size_t>(n));
            return 0;
        }

        float floorLookupGainFromIndex(int value)
        {
            return floorLookupGainTable[value];
        }

        static int roundDoubleToInt32(double value)
        {
            return static_cast<int>(std::lrint(value));
        }

        double vorbisCosLookup(float value)
        {
            const double scaled = static_cast<double>(value) * 40.74366592;
            const int index = roundDoubleToInt32(scaled - 0.5);
            return static_cast<double>(cosineLookupTable[index + 1] - cosineLookupTable[index])
                * (scaled - static_cast<double>(index))
                + static_cast<double>(cosineLookupTable[index]);
        }

        double vorbisInverseSqrtLookup(float value)
        {
            const double scaled = static_cast<double>(value) * 64.0 - 32.0;
            const int index = roundDoubleToInt32(scaled - 0.5);
            return static_cast<double>(inverseSqrtLookupTable[index + 1] - inverseSqrtLookupTable[index])
                * (scaled - static_cast<double>(index))
                + static_cast<double>(inverseSqrtLookupTable[index]);
        }

        double vorbisInverseSqrtExponentLookup(int value)
        {
            return static_cast<double>(inverseSqrtExponentTable[value]);
        }

        double vorbisFromDbLookup(float value)
        {
            const double scaled = static_cast<double>(value) * -8.0 - 0.5;
            const int index = roundDoubleToInt32(scaled);
            if (index < 0)
                return 1.0;
            if (index >= 1120)
                return 0.0;
            return static_cast<double>(fromDbFineLookupTable[index & 31])
                * static_cast<double>(fromDbCoarseLookupTable[index >> 5]);
        }

        int renderFloor0LspCurve(float* curve, const int* map, int n, int ln, float* lsp, int m, float amp, float ampoffset)
        {
            const float wdel = static_cast<float>(3.1415927 / static_cast<double>(ln));
            for (int i = 0; i < m; ++i)
                lsp[i] = static_cast<float>(vorbisCosLookup(lsp[i]));

            int i = 0;
            while (i < n)
            {
                const int k = map[i];
                const float w = static_cast<float>(vorbisCosLookup(static_cast<float>(k) * wdel));
                double p = 0.70710677;
                double q = 0.70710677;
                const int pairs = m >> 1;
                int lspIndex = 0;
                for (int pair = 0; pair < pairs; ++pair)
                {
                    p *= static_cast<double>(lsp[lspIndex] - w);
                    q *= static_cast<double>(lsp[lspIndex + 1] - w);
                    lspIndex += 2;
                }

                double sum;
                if (m & 1)
                {
                    p *= static_cast<double>(lsp[lspIndex] - w);
                    sum = p * p + (1.0 - static_cast<double>(w) * static_cast<double>(w)) * q * q;
                }
                else
                {
                    sum = p * p * (static_cast<double>(w) + 1.0)
                        + (1.0 - static_cast<double>(w)) * q * q;
                }

                int exponent = 0;
                const float mantissa = static_cast<float>(std::frexp(sum, &exponent));
                const float gainInput = static_cast<float>(vorbisInverseSqrtLookup(mantissa) * vorbisInverseSqrtExponentLookup(m + exponent) * static_cast<double>(amp) - static_cast<double>(ampoffset));
                const double gain = vorbisFromDbLookup(gainInput);
                do
                {
                    curve[i] = static_cast<float>(gain * static_cast<double>(curve[i]));
                    ++i;
                }
                while (map[i] == k);
            }

            return static_cast<int>(reinterpret_cast<std::uintptr_t>(curve + i) & 0xFFFFFFFFu);
        }

        static double floor0Bark(double frequency)
        {
            return 13.1 * std::atan(0.00074 * frequency)
                + 2.24 * std::atan(0.0000000185 * frequency * frequency)
                + 0.0001 * frequency;
        }


} } }
