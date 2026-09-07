#include "3rdparty/win/libvorbis/lib/codec_internal.h"

#include <cstddef>
#include <cstdlib>

namespace as1 { namespace thirdparty { namespace xiph2003
{
        void initializeBitCursor(BitCursor& cursor, const void* data, int size)
        {
            cursor.consumedBytes = 0;
            cursor.bitOffset = 0;
            cursor.bufferStart = static_cast<const unsigned char*>(data);
            cursor.cursor = static_cast<const unsigned char*>(data);
            cursor.bufferSize = size;
        }

        // uses the same physical 0x14-byte layout as BitCursor.  Keep the 2003
        // libogg 0x100-byte growth quantum and do not add modern allocation guards;
        // the retail owner dereferences the returned buffer immediately.
        void initializeBitWriter(BitCursor& cursor)
        {
            cursor.consumedBytes = 0;
            cursor.bitOffset = 0;
            cursor.bufferStart = nullptr;
            cursor.cursor = nullptr;
            cursor.bufferSize = 0;

            auto* buffer = static_cast<unsigned char*>(std::malloc(0x100));
            cursor.bufferStart = buffer;
            cursor.cursor = buffer;
            buffer[0] = 0;
            cursor.bufferSize = 0x100;
        }

        void alignBitWriter(BitCursor& cursor)
        {
            const int bits = 8 - cursor.bitOffset;
            if (bits < 8)
                writeBits(cursor, 0, bits);
        }

        void clearBitWriter(BitCursor& cursor)
        {
            std::free(const_cast<unsigned char*>(cursor.bufferStart));
            cursor.consumedBytes = 0;
            cursor.bitOffset = 0;
            cursor.bufferStart = nullptr;
            cursor.cursor = nullptr;
            cursor.bufferSize = 0;
        }

        // store when endbyte+4 reaches storage, in 0x100-byte increments, then
        // writes up to five bytes because an unaligned 32-bit value may cross the
        // fourth-byte boundary.
        void writeBits(BitCursor& cursor, unsigned int value, int bits)
        {
            if (cursor.consumedBytes + 4 >= cursor.bufferSize)
            {
                const int newSize = cursor.bufferSize + 0x100;
                auto* oldBuffer = const_cast<unsigned char*>(cursor.bufferStart);
                auto* newBuffer = static_cast<unsigned char*>(std::realloc(oldBuffer, static_cast<std::size_t>(newSize)));
                cursor.bufferStart = newBuffer;
                cursor.bufferSize = newSize;
                cursor.cursor = newBuffer + cursor.consumedBytes;
            }

            const unsigned int mask = static_cast<unsigned int>(bitMaskForWidth(bits));
            value &= mask;

            auto* out = const_cast<unsigned char*>(cursor.cursor);
            const int oldBitOffset = cursor.bitOffset;
            const int totalBits = oldBitOffset + bits;
            out[0] = static_cast<unsigned char>(out[0] | static_cast<unsigned char>(value << oldBitOffset));

            if (totalBits >= 8)
            {
                out[1] = static_cast<unsigned char>(value >> (8 - oldBitOffset));
                if (totalBits >= 16)
                {
                    out[2] = static_cast<unsigned char>(value >> (16 - oldBitOffset));
                    if (totalBits >= 24)
                    {
                        out[3] = static_cast<unsigned char>(value >> (24 - oldBitOffset));
                        if (totalBits >= 32)
                        {
                            if (oldBitOffset != 0)
                                out[4] = static_cast<unsigned char>(value >> (32 - oldBitOffset));
                            else
                                out[4] = 0;
                        }
                    }
                }
            }

            cursor.bitOffset = totalBits & 7;
            const int advance = totalBits / 8;
            cursor.consumedBytes += advance;
            cursor.cursor += advance;
        }

        int bitMaskForWidth(int bits)
        {
            if (bits <= 0)
                return 0;
            if (bits >= 32)
                return -1;
            return static_cast<int>((1u << bits) - 1u);
        }

        int readBits(BitCursor& cursor, int bits)
        {
            const int oldBitOffset = cursor.bitOffset;
            const int newBitOffset = oldBitOffset + bits;
            const int mask = bitMaskForWidth(bits);
            int value = -1;

            if (cursor.consumedBytes + 4 < cursor.bufferSize || newBitOffset + 8 * cursor.consumedBytes <= 8 * cursor.bufferSize)
            {
                unsigned int packed = 0;
                if (cursor.cursor && cursor.consumedBytes < cursor.bufferSize)
                {
                    packed = static_cast<unsigned int>(cursor.cursor[0]) >> (oldBitOffset & 0xFF);
                    if (newBitOffset > 8 && cursor.consumedBytes + 1 < cursor.bufferSize)
                    {
                        packed |= static_cast<unsigned int>(cursor.cursor[1]) << (8 - oldBitOffset);
                        if (newBitOffset > 16 && cursor.consumedBytes + 2 < cursor.bufferSize)
                        {
                            packed |= static_cast<unsigned int>(cursor.cursor[2]) << (16 - oldBitOffset);
                            if (newBitOffset > 24 && cursor.consumedBytes + 3 < cursor.bufferSize)
                            {
                                packed |= static_cast<unsigned int>(cursor.cursor[3]) << (24 - oldBitOffset);
                                if (newBitOffset > 32 && oldBitOffset && cursor.consumedBytes + 4 < cursor.bufferSize)
                                    packed |= static_cast<unsigned int>(cursor.cursor[4]) << (32 - oldBitOffset);
                            }
                        }
                    }
                }
                value = mask & static_cast<int>(packed);
            }

            if (cursor.cursor)
                cursor.cursor += newBitOffset / 8;
            cursor.consumedBytes += newBitOffset / 8;
            cursor.bitOffset = newBitOffset & 7;
            return value;
        }

        int lookBits(const BitCursor& cursor, int bits)
        {
            BitCursor copy = cursor;
            return readBits(copy, bits);
        }

        void advanceBits(BitCursor& cursor, int bits)
        {
            (void)readBits(cursor, bits);
        }

        int bitByteOffset(const BitCursor& cursor)
        {
            return cursor.consumedBytes + ((cursor.bitOffset + 7) / 8);
        }

        int readBytesFromPacket(BitCursor& cursor, void* target, int count)
        {
            int result = count - 1;
            auto* out = static_cast<unsigned char*>(target);
            for (int index = 0; index < count; ++index)
            {
                result = readBits(cursor, 8);
                if (out)
                    out[index] = static_cast<unsigned char>(result);
            }
            return result;
        }

        int countBitsForUnsignedValue(unsigned int value)
        {
            int result = 0;
            for (unsigned int remaining = value; remaining; remaining >>= 1)
                ++result;
            return result;
        }

        int countBitsForValueMinusOne(int value)
        {
            int result = 0;
            if (value)
            {
                unsigned int remaining = static_cast<unsigned int>(value - 1);
                if (value != 1)
                {
                    do
                    {
                        ++result;
                        remaining >>= 1;
                    } while (remaining);
                }
            }
            return result;
        }

        int populationCountUnsigned(unsigned int value)
        {
            int result = 0;
            for (unsigned int remaining = value; remaining; remaining >>= 1)
                result += static_cast<int>(remaining & 1u);
            return result;
        }

} } }
