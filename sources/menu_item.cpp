#include "menu_item.h"
#include <cstdint>
#include <cstring>
#include <string>

namespace as1
{
    namespace
    {
        constexpr int END_SPRITE_INT = -1;

        int readPayloadDword(const std::vector<BYTE>& payload, std::size_t offset)
        {
            if (offset + 4 > payload.size())
                return 0;
            int value = 0;
            std::memcpy(&value, payload.data() + offset, 4);
            return value;
        }

        bool isTextByte(BYTE ch)
        {
            return ch >= 0x20 && ch != 0x7F;
        }

        bool decodeMenuText(const std::vector<BYTE>& payload, std::size_t offset, STRING& outText, std::size_t& textEnd)
        {
            if (offset >= payload.size())
                return false;

            std::size_t pos = offset;
            while (pos < payload.size() && payload[pos] != 0)
            {
                if (!isTextByte(payload[pos]))
                    return false;
                ++pos;
            }

            if (pos == offset || pos >= payload.size())
                return false;

            const std::string text(reinterpret_cast<const char*>(payload.data() + offset), pos - offset);
            outText.Assign(text.c_str());
            textEnd = pos + 1;
            return true;
        }

        bool looksLikeMenuResourceLink(const STRING& text)
        {
            const std::string value = text.str();
            return value.find('\\') != std::string::npos ||
                   value.find('/') != std::string::npos ||
                   value.find('.') != std::string::npos;
        }
    }

    bool MENU_HEAD::Read(BaseStream& stream)
    {
        // loadMenuSpriteList reads exactly five 32-bit HEAD values before SPR/SPRI.
        return stream.read(&version, 4) == 0 &&
               stream.read(&sizeX, 4) == 0 &&
               stream.read(&sizeY, 4) == 0 &&
               stream.read(&shiftX, 4) == 0 &&
               stream.read(&shiftY, 4) == 0;
    }

    bool MENU_ITEM::Read(BaseStream& stream, int menuVersion)
    {
        sourceOrder = 0;
        subresourceSize = 0;
        terminator = false;
        commandPayload.clear();
        payloadRecord = MENU_PAYLOAD_RECORD{};

        if (stream.read(&oldAddress, 4) != 0)
            return false;
        if (oldAddress == END_SPRITE_INT)
        {
            terminator = true;
            return true;
        }

        if (stream.read(&nvid, 4) != 0)
            return false;

        if (menuVersion <= 9)
        {
            int x = 0;
            int y = 0;
            int z = 0;
            if (stream.read(&x, 4) != 0 || stream.read(&y, 4) != 0 || stream.read(&z, 4) != 0)
                return false;
            xyz = VECTOR(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
        }
        else
        {
            if (stream.read(&xyz.x, 4) != 0 || stream.read(&xyz.y, 4) != 0 || stream.read(&xyz.z, 4) != 0)
                return false;
        }

        direction.Read(&stream);
        return stream.read(&army, 4) == 0;
    }

    void MENU_ITEM::DecodeCommandPayload()
    {
        payloadRecord = MENU_PAYLOAD_RECORD{};
        if (commandPayload.empty())
            return;

        payloadRecord.present = true;
        payloadRecord.byteSize = static_cast<int>(commandPayload.size());
        if (commandPayload.size() >= 4)
            payloadRecord.commandId = readPayloadDword(commandPayload, 0);

        std::size_t textOffset = commandPayload.size();
        STRING decodedText;
        std::size_t textEnd = commandPayload.size();

        for (std::size_t offset = 4; offset < commandPayload.size(); ++offset)
        {
            STRING candidate;
            std::size_t candidateEnd = commandPayload.size();
            if (decodeMenuText(commandPayload, offset, candidate, candidateEnd))
            {
                decodedText = candidate;
                textOffset = offset;
                textEnd = candidateEnd;
                break;
            }
        }

        const std::size_t prefixEnd = textOffset == commandPayload.size() ? commandPayload.size() : textOffset;
        const std::size_t prefixWords = prefixEnd / 4;
        for (std::size_t word = 0; word < prefixWords; ++word)
            payloadRecord.prefixValues.push_back(readPayloadDword(commandPayload, word * 4));

        if (textOffset != commandPayload.size())
        {
            payloadRecord.hasTextReference = true;
            payloadRecord.textReference = decodedText;
            if (looksLikeMenuResourceLink(decodedText))
            {
                payloadRecord.hasResourceLink = true;
                payloadRecord.resourceLink = decodedText;
            }

            for (std::size_t i = textEnd; i < commandPayload.size(); ++i)
                payloadRecord.tailBytes.push_back(commandPayload[i]);
        }
        else
        {
            const std::size_t tailOffset = prefixWords * 4;
            for (std::size_t i = tailOffset; i < commandPayload.size(); ++i)
                payloadRecord.tailBytes.push_back(commandPayload[i]);
        }
    }

    void MENU_ITEM::ApplyMenuCentering(const MENU_HEAD& head, float viewportX, float viewportY)
    {
        // loadMenuSpriteList positions each loaded sprite around GRAPH width/height before
        // the optional 0x51 per-sprite menu command route.
        xyz.x = xyz.x - static_cast<float>(head.shiftX) - static_cast<float>(head.sizeX / 2) + viewportX * 0.5f;
        xyz.y = xyz.y - static_cast<float>(head.shiftY) - static_cast<float>(head.sizeY / 2) + viewportY * 0.5f;
    }
}
