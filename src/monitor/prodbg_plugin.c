#include "prodbg_plugin.h"
#include "pd_backend.h"
#include "pd_remote.h"
#include "mon_disassemble.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

//extern struct PDBackendPlugin s_debuggerPlugin;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct Debugger6502
{
	unsigned short pc;
	unsigned char sp;
	unsigned char a;
	unsigned char x;
	unsigned char y;
	int state;

} Debugger6502;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static Debugger6502 s_debugger;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void* createInstance(ServiceFunc* serviceFunc)
{
    (void)serviceFunc;
    return &s_debugger;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void destroyInstance(void* userData)
{
	(void)userData;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void writeRegister(PDWriter* writer, const char* name, uint8_t size, uint16_t reg, uint8_t readOnly)
{
    PDWrite_arrayEntryBegin(writer);
    PDWrite_string(writer, "name", name);
    PDWrite_u8(writer, "size", size);

    if (readOnly)
        PDWrite_u8(writer, "read_only", 1);

    if (size == 2)
    	PDWrite_u16(writer, "register", reg);
	else
    	PDWrite_u8(writer, "register", (uint8_t)reg);

    PDWrite_arrayEntryEnd(writer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void setRegisters(PDWriter* writer)
{
    PDWrite_eventBegin(writer, PDEventType_setRegisters);
    PDWrite_arrayBegin(writer, "registers");

    writeRegister(writer, "pc", 2, s_debugger.pc, 1);
    writeRegister(writer, "sp", 1, s_debugger.sp, 0);
    writeRegister(writer, "a", 1, s_debugger.a, 0);
    writeRegister(writer, "x", 1, s_debugger.x, 0);
    writeRegister(writer, "y", 1, s_debugger.y, 0);
    //writeRegister(writer, "status", 1, status, 1);

    PDWrite_arrayEnd(writer);
    PDWrite_eventEnd(writer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void setExceptionLocation(PDWriter* writer)
{
    PDWrite_eventBegin(writer,PDEventType_setExceptionLocation); 
    PDWrite_u16(writer, "address", s_debugger.pc);
    PDWrite_u8(writer, "address_size", 2);
    PDWrite_eventEnd(writer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int disassembleToBuffer(char* dest, int* addressIn, int* instCountIn)
{
    int i;
    int totalLength = 0;
    int len = 0;
    int address = *addressIn;
    int instCount = *instCountIn;

    // hacky (assume we can grab everything from zero)
    
    for (i = 0; i < instCount; ++i)
    {
    	unsigned int size;

		const char* code = mon_dis(e_comp_space, address, &size);
        len = (int)strlen(code);

        strncpy(dest, code, len);
        dest += len;

		totalLength += len;
    }

    dest[totalLength] = 0;

    //printf("TotalLength %d\n", totalLength);

    return totalLength;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void setDisassembly(PDWriter* writer, int start, int instCount)
{
    char temp[65536];

    disassembleToBuffer(temp, &start, &instCount);

    PDWrite_eventBegin(writer, PDEventType_setDisassembly);
    PDWrite_u16(writer, "address_start", (uint16_t)start);
    PDWrite_u16(writer, "instruction_count", (uint16_t)instCount);
    PDWrite_string(writer, "string_buffer", temp);
    PDWrite_eventEnd(writer);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/*
static void getDisassembly(PDReader* reader, PDWriter* writer)
{
    uint16_t start = 0;
    uint32_t instCount = 1;

    PDRead_findU16(reader, &start, "address_start", 0);
    PDRead_findU32(reader, &instCount, "instruction_count", 0);

    setDisassembly(writer, start, instCount);
}
*/

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void sendState(PDWriter* writer)
{
    setExceptionLocation(writer);
    setRegisters(writer);
	setDisassembly(writer, 0, 10);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void doAction(Debugger6502* debugger, PDAction action, PDWriter* writer)
{
    int t = (int)action;

    switch (t)
    {
        case PDAction_break : 
        {
            // On this target we can anways break so just set that we have stopped on breakpoint
            
            printf("Fake6502Debugger: break\n");
            debugger->state = PDDebugState_stopException;
            sendState(writer);
            break;
        }

        case PDAction_run : 
        {
            // on this target we can always start running directly again
            printf("Fake6502Debugger: run\n");
            debugger->state = PDDebugState_running;
            break;
        }

        case PDAction_step : 
        {
            // on this target we can always stepp 
            printf("Fake6502Debugger: step\n");
            debugger->state = PDDebugState_trace;
            sendState(writer);
            break;
        }
    }
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static PDDebugState update(void* userData, PDAction action, PDReader* reader, PDWriter* writer)
{
    //int event = 0;

    (void)reader;

    Debugger6502* debugger = (Debugger6502*)userData;

    doAction(debugger, action, writer);

    return debugger->state;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

PDBackendPlugin s_debuggerPlugin =
{
    "VICE",
    createInstance,
    destroyInstance,
    update,
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int prodbg_plugin_state()
{
	Debugger6502* debugger = &s_debugger;

	switch (debugger->state)
	{
		case PDDebugState_trace:
		case PDDebugState_stopBreakpoint:
		case PDDebugState_stopException : 
			return PRODBG_STATE_TRACE; 
	}

	return PRODBG_STATE_RUNNING;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

int prodbg_plugin_init()
{
	return PDRemote_create(&s_debuggerPlugin, 0) - 1;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void prodbg_plugin_update()
{
	PDRemote_update(0);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void prodbg_plugin_set_state(int pc, int a, int x, int y, int sp)
{
	Debugger6502* debugger = &s_debugger;

	debugger->pc = pc;
	debugger->a = a;
	debugger->x = x;
	debugger->y = y;
	debugger->sp = sp;

	if (debugger->state != PDDebugState_stopException)
		return;

	for (;;)
	{
		switch (debugger->state)
		{
			case PDDebugState_running : 
				return;

			case PDDebugState_trace : 
			{
				printf("trace\n");
				debugger->state = PDDebugState_stopException;
				return;
			}

			default : break;
		}

		PDRemote_update(1);
	}
}


