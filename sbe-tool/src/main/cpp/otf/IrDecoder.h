/*
 * Copyright 2015 Real Logic Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _OTF_IRDECODER_H
#define _OTF_IRDECODER_H

#if defined(WIN32) || defined(_WIN32)
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#define fileno _fileno
#define read _read
#define stat _stat64
#else
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/stat.h>
#endif /* WIN32 */

#include <memory>
#include <exception>
#include <vector>
#include <functional>
#include <algorithm>

#include "uk_co_real_logic_sbe_ir_generated/TokenCodec.hpp"
#include "uk_co_real_logic_sbe_ir_generated/FrameCodec.hpp"
#include "Token.h"

using namespace sbe::otf;
using namespace uk_co_real_logic_sbe_ir_generated;

namespace sbe {
namespace otf {

class IrDecoder
{
public:
    IrDecoder() :
        m_buffer(nullptr), m_length(0)
    {
    }

    ~IrDecoder()
    {
        if (nullptr != m_buffer)
        {
            delete [] m_buffer;
        }
    }

    int decode(char *buffer, int length)
    {
        return decodeIr();
    }

    int decode(const char *filename)
    {
        if ((m_length = getFileSize(filename)) < 0)
        {
            return -1;
        }
        std::cout << "IR Filename " << filename << " length " << m_length << std::endl;
        if (m_length == 0)
        {
            return -1;
        }
        m_buffer = new char[m_length];

        if (readFileIntoBuffer(m_buffer, filename, m_length) < 0)
        {
            return -1;
        }

        return decode(m_buffer, m_length);
    }

    std::shared_ptr<std::vector<Token>> header()
    {
        return m_headerTokens;
    }

    std::vector<std::shared_ptr<std::vector<Token>>> messages()
    {
        return m_messages;
    }

    std::shared_ptr<std::vector<Token>> message(int id, int version)
    {
        std::shared_ptr<std::vector<Token>> result;

        std::for_each(m_messages.begin(), m_messages.end(),
            [&](std::shared_ptr<std::vector<Token>> tokens)
            {
                Token& token = tokens->at(0);

                if (token.signal() == Signal::BEGIN_MESSAGE && token.fieldId() == id && token.tokenVersion() == version)
                {
                    result = tokens;
                }
            });

        return result;
    }

protected:
    // OS specifics
    static int getFileSize(const char *filename)
    {
        struct stat fileStat;

        if (::stat(filename, &fileStat) != 0)
        {
            return -1;
        }

        return fileStat.st_size;
    }

    static int readFileIntoBuffer(char *buffer, const char *filename, int length)
    {
        FILE *fptr = ::fopen(filename, "rb");
        int remaining = length;

        if (fptr == NULL)
        {
            return -1;
        }

        int fd = fileno(fptr);
        while (remaining > 0)
        {
            int sz = ::read(fd, buffer + (length - remaining), (4098 < remaining) ? 4098 : remaining);
            remaining -= sz;
            if (sz < 0)
            {
                break;
            }
        }

        fclose(fptr);

        return (remaining == 0) ? 0 : -1;
    }

private:
    std::shared_ptr<std::vector<Token>> m_headerTokens;
    std::vector<std::shared_ptr<std::vector<Token>>> m_messages;
    char *m_buffer;
    int m_length;
    int m_id;

    int decodeIr()
    {
        FrameCodec frame;
        int offset = 0, tmpLen = 0;
        char tmp[256];

        frame.wrapForDecode(m_buffer, offset, frame.sbeBlockLength(), frame.sbeSchemaVersion(), m_length);

        tmpLen = frame.getPackageName(tmp, sizeof(tmp));
        std::cout << "Reading IR package=\"" << std::string(tmp, tmpLen) << "\" id=" << frame.irId() << "\n";

        if (frame.irVersion() != 0)
        {
            std::cerr << "unknown SBE IR version: " << frame.irVersion() << "\n";
            return -1;
        }

        frame.getNamespaceName(tmp, sizeof(tmp));
        frame.getSemanticVersion(tmp, sizeof(tmp));

        offset += frame.size();

        m_headerTokens.reset(new std::vector<Token>());

        int headerLength = readHeader(offset);

        m_id = frame.irId();

        offset += headerLength;

        while (offset < m_length)
        {
            offset += readMessage(offset);
        }

        return 0;
    }

    static void getString(std::string& str, long strLen, const char *ptr)
    {
        str.assign(ptr, static_cast<unsigned long>(strLen));
    }

    int decodeAndAddToken(std::shared_ptr<std::vector<Token>> tokens, int offset)
    {
        TokenCodec tokenCodec;
        tokenCodec.wrapForDecode(m_buffer, offset, tokenCodec.sbeBlockLength(), tokenCodec.sbeSchemaVersion(), m_length);

        Signal signal = static_cast<Signal>(tokenCodec.signal());
        PrimitiveType type = static_cast<PrimitiveType>(tokenCodec.primitiveType());
        Presence presence = static_cast<Presence>(tokenCodec.presence());
        ByteOrder byteOrder = static_cast<ByteOrder>(tokenCodec.byteOrder());
        int tokenOffset = tokenCodec.tokenOffset();
        int tokenSize = tokenCodec.tokenSize();
        int id = tokenCodec.fieldId();
        int version = tokenCodec.tokenVersion();
        int componentTokenCount = tokenCodec.componentTokenCount();
        std::string name;
        std::string characterEncoding;
        std::string epoch;
        std::string timeUnit;
        std::string semanticType;
        std::string description;

        getString(name, tokenCodec.nameLength(), tokenCodec.name());

        PrimitiveValue constValue(type, tokenCodec.constValueLength(), tokenCodec.constValue());
        PrimitiveValue minValue(type, tokenCodec.minValueLength(), tokenCodec.minValue());
        PrimitiveValue maxValue(type, tokenCodec.maxValueLength(), tokenCodec.maxValue());
        PrimitiveValue nullValue(type, tokenCodec.nullValueLength(), tokenCodec.nullValue());

        getString(characterEncoding, tokenCodec.characterEncodingLength(), tokenCodec.characterEncoding());
        getString(epoch, tokenCodec.epochLength(), tokenCodec.epoch());
        getString(timeUnit, tokenCodec.timeUnitLength(), tokenCodec.timeUnit());
        getString(semanticType, tokenCodec.semanticTypeLength(), tokenCodec.semanticType());
        getString(description, tokenCodec.descriptionLength(), tokenCodec.description());

        Encoding encoding(
            type, presence, byteOrder, minValue, maxValue, nullValue, constValue,
            characterEncoding, epoch, timeUnit, semanticType);

        Token token(
            tokenOffset, id, version, tokenSize, componentTokenCount, signal, name, description, encoding);

        tokens->push_back(token);

        return tokenCodec.size();
    }

    int readHeader(int offset)
    {
        int size = 0;

        while (offset + size < m_length)
        {
            size += decodeAndAddToken(m_headerTokens, offset + size);

            Token& token = m_headerTokens->back();

            if (token.signal() == Signal::BEGIN_COMPOSITE)
            {
                std::cout << " Header name=\"" << token.name() << "\"";
            }

            if (token.signal() == Signal::END_COMPOSITE)
            {
                break;
            }
        }

        std::cout << " length " << size << std::endl;

        return size;
    }

    int readMessage(int offset)
    {
        int size = 0;

        std::shared_ptr<std::vector<Token>> tokensForMessage(new std::vector<Token>());

        while (offset + size < m_length)
        {
            size += decodeAndAddToken(tokensForMessage, offset + size);

            Token& token = tokensForMessage->back();

            if (token.signal() == Signal::BEGIN_MESSAGE)
            {
                std::cout << " Message name=\"" << token.name() << "\"";
                std::cout << " id=\"" << token.fieldId() << "\"";
                std::cout << " version=\"" << token.tokenVersion() << "\"";
            }

            if (token.signal() == Signal::END_MESSAGE)
            {
                break;
            }
        }

        std::cout << " length " << size << std::endl;

        m_messages.push_back(tokensForMessage);

        return size;
    }
};

}}

#endif
