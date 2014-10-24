#include "log.h"
#include <stdio.h>
#include <stdarg.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif


static int s_log_level = 0;
static int s_old_level = 0;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void pda_log_out(int logLevel, const char* filename, int line, const char* format, ...)
{
    va_list ap;

    if (logLevel < s_log_level)
        return;

    switch (logLevel)
    {
        case LOG_DEBUG:
            printf("%s:%d [DEBUG] ", filename, line); break;
        case LOG_INFO:
            printf("%s:%d [INFO]  ", filename, line); break;
        case LOG_ERROR:
            printf("%s:%d [ERROR] ", filename, line); break;
    }

    va_start(ap, format);
#if defined(_WIN32)
    {
        char buffer[2048];
        vsprintf(buffer, format, ap);
        OutputDebugStringA(buffer);
    }
#else
    vprintf(format, ap);
#endif
    va_end(ap);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void pda_log_set_level(int logLevel)
{
    s_log_level = logLevel;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void pda_log_level_push()
{
    s_old_level = s_log_level;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void pda_log_level_pop()
{
    s_log_level = s_old_level;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

