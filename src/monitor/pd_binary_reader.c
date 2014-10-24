#include <pd_readwrite.h>
#include "pd_readwrite_private.h"
#include "log.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct ReaderData
{
    uint8_t* data;
    uint8_t* dataStart;
    uint8_t* dataEnd;
    uint8_t* nextEvent;
} ReaderData;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#ifdef _WIN32
#define inline __inline
#endif

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

union Convert
{
    double dv;
    float fv;
    uint64_t u32;
    uint64_t u64;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline int8_t getS8(const uint8_t* ptr)
{
    return (int8_t)ptr[0];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint8_t getU8(const uint8_t* ptr)
{
    return ptr[0];
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint16_t getS16(const uint8_t* ptr)
{
    int16_t v = (ptr[0] << 8) | ptr[1];
    return v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint16_t getU16(const uint8_t* ptr)
{
    uint16_t v = (ptr[0] << 8) | ptr[1];
    return v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline int32_t getS32(const uint8_t* ptr)
{
    int32_t v = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
    return v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline int32_t getU32(const uint8_t* ptr)
{
    uint32_t v = (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | ptr[3];
    return v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline int64_t getS64(const uint8_t* ptr)
{
    int64_t v = ((uint64_t)ptr[0] << 56) | ((uint64_t)ptr[1] << 48) | ((uint64_t)ptr[2] << 40) | ((uint64_t)ptr[3] << 32) |
                (ptr[4] << 24) | (ptr[5] << 16) | (ptr[6] << 8) | ptr[7];
    return v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint64_t getU64(const uint8_t* ptr)
{
    uint64_t v = ((uint64_t)ptr[0] << 56) | ((uint64_t)ptr[1] << 48) | ((uint64_t)ptr[2] << 40) | ((uint64_t)ptr[3] << 32) |
                 (ptr[4] << 24) | (ptr[5] << 16) | (ptr[6] << 8) | ptr[7];
    return v;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline double getDouble(const uint8_t* ptr)
{
    union Convert c;
    c.u64 = getU64(ptr);
    return c.dv;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline float getFloat(const uint8_t* ptr)
{
    union Convert c;
    c.u32 = getU32(ptr);
    return c.fv;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static inline uint64_t getOffsetUpper(ReaderData* readerData, const uint8_t* data)
{
    uint64_t t = ((uintptr_t)data - (uintptr_t)readerData->dataStart);
    return t << 32L;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readGetEvent(struct PDReader* reader)
{
    ReaderData* rData = (ReaderData*)reader->data;
    uint16_t event;
    uint8_t type;
    uint8_t* data;

    if (!rData->data)
    {
        log_debug("no data");
        return 0;
    }

    if (rData->nextEvent >= rData->dataEnd)
    {
        log_debug("rData->nextEvent %p >= rData->dataEnd %p\n", rData->nextEvent, rData->dataEnd);
        return 0;
    }

    // if this is not set we expect this to be the first event and just read from data

    if (!rData->nextEvent)
        data = rData->data;
    else
        data = rData->nextEvent;

    // make sure we actually have some data to process

    if (data >= rData->dataEnd - 4)
    {
        log_debug("data %p >= rData->dataEnd - 4 %p\n", data, rData->dataEnd - 4);
        return 0;
    }

    type = *data;

    if (type != PDReadType_event)
    {
        log_info("Unable to read event as type is wrong (expected %d but got %d) all read operations will now fail.\n",
                 PDReadType_event, type);
        return 0;
    }

    event = getU16(data + 1);
    rData->nextEvent = data + getU32(data + 3);
    rData->data = data + 7; // points to the next of data in the stream

    log_debug("returing with event %d\n", event);

    return event;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static const char* typeTable[] =
{
    "PDReadType_none",
    "PDReadType_s8",
    "PDReadType_u8",
    "PDReadType_s16",
    "PDReadType_u16",
    "PDReadType_s32",
    "PDReadType_u32",
    "PDReadType_s64",
    "PDReadType_u64",
    "PDReadType_float",
    "PDReadType_double",
    "PDReadType_endNumericTypes",
    "PDReadType_string",
    "PDReadType_data",
    "PDReadType_event",
    "PDReadType_array",
    "PDReadType_arrayEntry",
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint8_t* findIdByRange(const char* id, uint8_t* start, uint8_t* end)
{
    while (start < end)
    {
        uint32_t size;
        uint8_t typeId = getU8(start);

        ///if (typeId <PDReadType_count)
        //	log_debug("typeId %s\n", typeTable[typeId]);
        //else
        //	log_debug("typeId %d (outside valid range)\n", typeId);

        // data is a special case as it has 32-bit size instead of 64k

        if (typeId == PDReadType_data || typeId == PDReadType_array)
        {
            size = getU32(start + 1);

            if (!strcmp((char*)start + 5, id))
                return start;
        }
        else
        {
            size = getU16(start + 1);

            //log_debug("current string - %s searching for - %s\n", (char*)start + 3, id);

            if (!strcmp((char*)start + 3, id))
                return start;
        }

        start += size;
    }

    // not found
    //

    return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint8_t* findId(struct PDReader* reader, const char* id, PDReaderIterator it)
{
    ReaderData* rData = (ReaderData*)reader->data;

    // if iterator is 0 we search the event stream,

    if (it == 0)
    {
        // if no iterater we will just search the whole event
        return findIdByRange(id, rData->data, rData->nextEvent);
    }
    else
    {
        // serach within the event but skip 7 bytes ahead to not read the event itself
        uint32_t dataOffset = it >> 32LL;
        uint32_t size = it & 0xffffffffLL;
        uint8_t* start = rData->dataStart + dataOffset;
        uint8_t* end = start + size;
        return findIdByRange(id, start, end);
    }
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define findValue(inType, realType, getFunc) \
    uint8_t type; \
    size_t offset; \
    const uint8_t* dataPtr = findId(reader, id, it); \
    if (!dataPtr) \
        return PDReadStatus_notFound; \
    type = *dataPtr; \
    offset = getU16(dataPtr + 1) - (sizeof(realType)); \
    if (type == inType) \
    { \
        *res = getFunc(dataPtr + offset); \
        return PDReadStatus_ok | inType; \
    } \
    if (type < PDReadType_endNumericTypes) \
    { \
        offset = strlen((const char*)dataPtr + 3) + 4; \
        switch (type) \
        { \
            case PDReadType_s8: \
                *res = (realType)getS8(dataPtr + offset); return PDReadType_s8 | PDReadStatus_converted; \
            case PDReadType_u8: \
                *res = (realType)getU8(dataPtr + offset); return PDReadType_u8 | PDReadStatus_converted;  \
            case PDReadType_s16: \
                *res = (realType)getU16(dataPtr + offset); return PDReadType_s16 | PDReadStatus_converted; \
            case PDReadType_u16: \
                *res = (realType)getU16(dataPtr + offset); return PDReadType_u16 | PDReadStatus_converted; \
            case PDReadType_s32: \
                *res = (realType)getU32(dataPtr + offset); return PDReadType_s32 | PDReadStatus_converted; \
            case PDReadType_u32: \
                *res = (realType)getU32(dataPtr + offset); return PDReadType_u32 | PDReadStatus_converted; \
            case PDReadType_s64: \
                *res = (realType)getU64(dataPtr + offset); return PDReadType_s64 | PDReadStatus_converted; \
            case PDReadType_u64: \
                *res = (realType)getU64(dataPtr + offset); return PDReadType_u64 | PDReadStatus_converted; \
            case PDReadType_float: \
                *res = (realType)getFloat(dataPtr + offset); return PDReadType_float | PDReadStatus_converted; \
            case PDReadType_double: \
                *res = (realType)getDouble(dataPtr + offset); return PDReadType_float | PDReadStatus_converted; \
        } \
    } \
    return (PDReadType)type | PDReadStatus_illegalType

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS8(struct PDReader* reader, int8_t* res, const char* id, PDReaderIterator it)
{
    findValue(PDReadType_s8, int8_t, getS8);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU8(struct PDReader* reader, uint8_t* res, const char* id, PDReaderIterator it)
{
    findValue(PDReadType_u8, uint8_t, getU8);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS16(struct PDReader* reader, int16_t* res, const char* id, PDReaderIterator it)
{
    findValue(PDReadType_s16, int16_t, getS16);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU16(struct PDReader* reader, uint16_t* res, const char* id, PDReaderIterator it)
{
    findValue(PDReadType_u16, uint16_t, getU16);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS32(struct PDReader* reader, int32_t* res, const char* id, PDReaderIterator it)
{
    findValue(PDReadType_s32, int32_t, getS32);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU32(struct PDReader* reader, uint32_t* res, const char* id, PDReaderIterator it)
{
    findValue(PDReadType_u32, uint32_t, getU32);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindS64(struct PDReader* reader, int64_t* res, const char* id, PDReaderIterator it)
{
    findValue(PDReadType_s64, int64_t, getS64);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindU64(struct PDReader* reader, uint64_t* res, const char* id, PDReaderIterator it)
{
    findValue(PDReadType_u64, uint64_t, getU64);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindFloat(struct PDReader* reader, float* res, const char* id, PDReaderIterator it)
{
    findValue(PDReadType_float, float, getFloat);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindDouble(struct PDReader* reader, double* res, const char* id, PDReaderIterator it)
{
    findValue(PDReadType_double, double, getDouble);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindString(struct PDReader* reader, const char** res, const char* id, PDReaderIterator it)
{
    uint8_t type;
    int len;

    const uint8_t* dataPtr = findId(reader, id, it);
    if (!dataPtr)
        return PDReadStatus_notFound;

    type = *dataPtr;

    if (type != PDReadType_string)
        return (PDReadType)type | PDReadStatus_illegalType;

    // find the offset to the string

    len = (int)strlen((char*)dataPtr + 3);
    *res = (const char*)dataPtr + 3 + len + 1;

    return (PDReadType)type | PDReadStatus_ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindData(struct PDReader* reader, void** data, uint64_t* size, const char* id, PDReaderIterator it)
{
    uint8_t type;
    int idLength;

    const uint8_t* dataPtr = findId(reader, id, it);
    if (!dataPtr)
        return PDReadStatus_notFound;

    type = *dataPtr;

    if (type != PDReadType_data)
        return (PDReadType)type | PDReadStatus_illegalType;

    idLength = (int)strlen((char*)dataPtr + 5) + 1;

    // find the offset to the string

    *size = getU32(dataPtr + 1) - idLength;
    *data = (void*)(dataPtr + 5 + idLength);

    return PDReadType_data | PDReadStatus_ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFindArray(struct PDReader* reader, PDReaderIterator* arrayIt, const char* id, PDReaderIterator it)
{
    uint8_t type;
    int idLength;
    ReaderData* rData = (ReaderData*)reader->data;

    const uint8_t* dataPtr = findId(reader, id, it);
    if (!dataPtr)
    {
        return PDReadStatus_notFound;
    }

    type = *dataPtr;

    if (type != PDReadType_array)
        return (PDReadType)type | PDReadStatus_illegalType;

    idLength = (int)strlen((char*)dataPtr + 5) + 1;

    // get offset to the array entry

    dataPtr += idLength + 5;

    // find the offset to the string

    *arrayIt = getOffsetUpper(rData, dataPtr);

    return PDReadType_array | PDReadStatus_ok;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static int32_t readNextEntry(struct PDReader* reader, PDReaderIterator* arrayIt)
{
    uint8_t type;
    int32_t entries;
    uint64_t it = *arrayIt;

    // we use a small trick here: The first time we get here the size will be zero (as set by find/readArray)
    // so when we are supposed to jump to the next entry we jump to the current one which is correct as we
    // want to read out the size to the next one (which we will jump to the next time we get here

    ReaderData* rData = (ReaderData*)reader->data;
    uint32_t offset = it >> 32LL;
    uint32_t size = it & 0xffffffffLL;
    uint8_t* entryStart = rData->dataStart + offset + size;

    if ((type = *entryStart) != PDReadType_arrayEntry)
    {
        log_info("No arrayEntry found at %p (found %d) but expected %d\n", entryStart, type, PDReadType_arrayEntry);
        return -1;
    }

    // get size and number of entries

    size = getU32(entryStart + 1);
    entries = getS16(entryStart + 5);

    // at the first entry this is the same as arrayStar

    *arrayIt = getOffsetUpper(rData, entryStart + 7) | (size - 7);

    return entries;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS8(struct PDReader* reader, int8_t* res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU8(struct PDReader* reader, uint8_t* res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS16(struct PDReader* reader, int16_t* res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU16(struct PDReader* reader, uint16_t* res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS32(struct PDReader* reader, int32_t* res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU32(struct PDReader* reader, uint32_t* res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readS64(struct PDReader* reader, int64_t* res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readU64(struct PDReader* reader, uint64_t* res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readFloat(struct PDReader* reader, float* res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readDouble(struct PDReader* reader, double* res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readString(struct PDReader* reader, const char** res, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readData(struct PDReader* reader, void** res, unsigned int* size, const char** id, PDReaderIterator* it)
{
    (void)reader;
    (void)res;
    (void)size;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t readArray(struct PDReader* reader, PDReaderIterator* arrayIt, const char* id, PDReaderIterator* it)
{
    (void)reader;
    (void)arrayIt;
    (void)id;
    (void)it;
    return PDReadType_none | PDReadStatus_fail;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void readDumpData(struct PDReader* reader)
{
    int eventId;
    ReaderData* rData = (ReaderData*)reader->data;

    while ((eventId = PDRead_getEvent(reader)) != 0)
    {
        log_info("{ = event %d - (start %p end %p)\n", eventId, rData->data, rData->nextEvent);

        while (rData->data < rData->nextEvent)
        {
            uint8_t type = *(uint8_t*)rData->data;
            uint32_t size = getU16(rData->data + 1);
            const char* idOffset = (const char*)rData->data + 3;

            if (type < PDReadType_count)
            {
                if (type == PDReadType_array)
                {
                    // need to handle array here, now just grab the correct size and idOffset

                    size = getU32(rData->data + 1);
                    idOffset = (const char*)rData->data + 5;
                }

                log_info("  %s : (%s - %d)\n", idOffset, typeTable[type], size);
            }

            rData->data += size;
        }

        log_info("}\n");
    }

    PDBinaryReader_reset(reader);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PDBinaryReader_init(PDReader* reader)
{
    reader->readGetEvent = readGetEvent;
    reader->readNextEntry = readNextEntry;
    reader->readFindS8 = readFindS8;
    reader->readFindU8 = readFindU8;
    reader->readFindS16 = readFindS16;
    reader->readFindU16 = readFindU16;
    reader->readFindS32 = readFindS32;
    reader->readFindU32 = readFindU32;
    reader->readFindS64 = readFindS64;
    reader->readFindU64 = readFindU64;
    reader->readFindFloat = readFindFloat;
    reader->readFindDouble = readFindDouble;
    reader->readFindString = readFindString;
    reader->readFindData = readFindData;
    reader->readFindArray = readFindArray;
    reader->readS8 = readS8;
    reader->readU8 = readU8;
    reader->readS16 = readS16;
    reader->readU16 = readU16;
    reader->readS32 = readS32;
    reader->readU32 = readU32;
    reader->readS64 = readS64;
    reader->readU64 = readU64;
    reader->readFloat = readFloat;
    reader->readDouble = readDouble;
    reader->readString = readString;
    reader->readData = readData;
    reader->readArray = readArray;
    reader->readDumpData = readDumpData;

    reader->data = malloc(sizeof(ReaderData));
    memset(reader->data, 0, sizeof(ReaderData));
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PDBinaryReader_initStream(PDReader* reader, uint8_t* data, unsigned int size)
{
    ReaderData* readerData = (ReaderData*)reader->data;
    readerData->data = readerData->dataStart = data + 4;    // top 4 bytes for size + 2 bits for info
    readerData->dataEnd = (uint8_t*)data + size;
    readerData->nextEvent = 0;
    pda_log_set_level(LOG_INFO);
    log_debug("InitStream %p - size %d\n", data, size);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PDBinaryReader_reset(PDReader* reader)
{
    ReaderData* readerData = (ReaderData*)reader->data;
    readerData->data = readerData->dataStart;
    readerData->nextEvent = 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void PDBinaryReader_destroy(PDReader* reader)
{
    ReaderData* readerData = (ReaderData*)reader->data;
    free(readerData);
}

