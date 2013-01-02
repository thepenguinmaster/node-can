/* Copyright Sebastian Haas <sebastian@sebastianhaas.info>. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#define __STDC_LIMIT_MACROS

#include <v8.h>
#include <node.h>

#include <algorithm>

#include <stdint.h>
#include <string.h>

using namespace node;
using namespace v8;

#define CHECK_CONDITION(expr, str) if(! (expr) ) return ThrowException(Exception::Error(String::New(str)));

typedef enum ENDIANESS
{
    ENDIANESS_MOTOROLA = 0,
    ENDIANESS_INTEL
} ENDIANESS;

//-----------------------------------------------------------------------------------------
// _signals.* methods

static u_int64_t _getvalue(u_int8_t * data,
                           u_int32_t offset,
                           u_int32_t length,
                           ENDIANESS byteOrder)
{
    uint64_t d = be64toh(*((uint64_t *)&data[0]));
    uint64_t o = 0;

    if (byteOrder == ENDIANESS_INTEL)
    {
        d <<= offset;

        size_t i, left = length;

        for (i = 0; i < length;)
        {
            size_t next_shift = left >= 8 ? 8 : left;
            size_t shift = 64 - (i + next_shift);
            size_t m = next_shift < 8 ? 0xFF >> next_shift : 0xFF;

            o |= ((d >> shift) & m) << i;

            left -= 8;
            i += next_shift;
        }
    }
    else
    {
        uint64_t m = UINT64_MAX;
        size_t shift = 64 - offset - 1;

        m = (1 << length) - 1;
        o = (d >> shift) & m;
    }

    return o;
}

// Decode signal according description
// arg[0] - Data array
// arg[1] - offset zero indexed
// arg[3] - bitLength one indexed
// arg[4] - endianess
Handle<Value> DecodeSignal(const Arguments& args)
{
    HandleScope scope;

    u_int32_t offset, bitLength;
    ENDIANESS endianess;
    bool isSigned = false;
    u_int8_t data[8];

    CHECK_CONDITION(args.Length() >= 4, "Too few arguments");
    CHECK_CONDITION(args[0]->IsArray(), "Invalid argument");
    CHECK_CONDITION(args[1]->IsUint32(), "Invalid offset");
    CHECK_CONDITION(args[2]->IsUint32(), "Invalid bit length");
    CHECK_CONDITION(args[3]->IsBoolean(), "Invalid endianess");

    Local<Array> jsData = Local<Array>::Cast(args[0]);

    offset = args[1]->ToUint32()->Uint32Value();
    bitLength = args[2]->ToUint32()->Uint32Value();
    endianess = args[3]->IsTrue() ? ENDIANESS_INTEL : ENDIANESS_MOTOROLA;

    if (args[4]->IsBoolean())
        isSigned = args[4]->IsTrue() ? true : false;

    int i;
    size_t maxBytes = std::min<u_int32_t>(jsData->Length(), sizeof(data));

    // Remember bytes in given message data array
    for (i = 0; i < maxBytes; i++)
        data[i] = (u_int8_t)jsData->Get(i)->Int32Value();

    Local<Integer> retval;
    uint64_t val = _getvalue(data, offset, bitLength, endianess);

    // Value shall be interpreted as signed (2's complement)
    if (isSigned && val & (1 << (bitLength - 1))) {
        int32_t tmp = -1 * (~((UINT64_MAX << bitLength) | val) + 1);
	retval = Integer::New(tmp);
    } else {
	retval = Integer::NewFromUnsigned((u_int32_t)val);
    }

    return scope.Close(retval);
}

void _setvalue(u_int32_t offset, u_int32_t bitLength, ENDIANESS endianess, u_int8_t data[8], u_int64_t raw_value)
{
    uint64_t o = be64toh(*(uint64_t *)&data[0]);

    if (endianess == ENDIANESS_INTEL)
    {
        size_t left = bitLength;

        size_t source = 0;

        for (source = 0; source < bitLength; )
        {
            size_t next_shift = left < 8 ? left : 8;
            size_t shift = (64 - offset - next_shift) - source;
            uint64_t m = ((1 << next_shift) - 1);

            o &= ~(m << shift);
            o |= (raw_value & m) << shift;

            raw_value >>= 8;
            source += next_shift;
            left -= next_shift;
        }
    }
    else
    {
        uint64_t m = ((1 << bitLength) - 1);
        size_t shift = 64 - offset - 1;

        o &= ~(m << shift);
        o |= (raw_value & m) << shift;
    }

    o = htobe64(o);

    memcpy(&data[0], &o, 8);
}

// Encode signal according description
// arg[0] - Data array
// arg[1] - startByte one indexed, One indexed, Left(1)->Right(8)
// arg[2] - startBit zero indexed, Right(0)->Left(7)
// arg[3] - bitLength one indexed
// arg[4] - endianess
// arg[5] - value to encode
Handle<Value> EncodeSignal(const Arguments& args)
{
    HandleScope scope;

    u_int32_t offset, bitLength;
    ENDIANESS endianess;
    u_int8_t data[8];
    u_int64_t raw_value;

    CHECK_CONDITION(args.Length() >= 5, "Too few arguments");
    CHECK_CONDITION(args[0]->IsArray(), "Invalid argument");
    CHECK_CONDITION(args[1]->IsUint32(), "Invalid offset");
    CHECK_CONDITION(args[2]->IsUint32(), "Invalid bit length");
    CHECK_CONDITION(args[3]->IsBoolean(), "Invalid endianess");
    CHECK_CONDITION(args[4]->IsNumber() || args[5]->IsBoolean(), "Invalid value");

    Local<Array> jsData = Local<Array>::Cast(args[0]);

    offset = args[1]->ToUint32()->Uint32Value();
    bitLength = args[2]->ToUint32()->Uint32Value();
    endianess = args[3]->IsTrue() ? ENDIANESS_INTEL : ENDIANESS_MOTOROLA;

    raw_value = args[4]->ToNumber()->Uint32Value();

    int i;
    size_t maxBytes = std::min<u_int32_t>(jsData->Length(), sizeof(data));

    // Remember bytes in given message data array
    for (i = 0; i < maxBytes; i++)
        data[i] = (u_int8_t)jsData->Get(i)->Int32Value();

    _setvalue(offset, bitLength, endianess, data, raw_value);

    // Restore new bytes
    for (i = 0; i < maxBytes; i++)
        jsData->Set(i, Integer::NewFromUnsigned((u_int32_t)data[i]));

    return scope.Close(Undefined());
}

//-----------------------------------------------------------------------------------------

extern "C" {
  static void init (Handle<Object> target)
  {
    NODE_SET_METHOD(target, "decode_signal", DecodeSignal);
    NODE_SET_METHOD(target, "encode_signal", EncodeSignal);
  }

  NODE_MODULE(can_signals, init);
}