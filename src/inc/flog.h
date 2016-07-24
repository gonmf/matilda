/*
Support for logging to file. Logging is made to a file called
matilda_YYMMDD_XXXXXX.log where YYMMDD is the date and XXXXXX is a random
string. When logging a mask of log categories specifies the types of messages to
be written to file. Having a very high degree of detail in very fast matches
actively hurts the performance.

Writing to files is synchronous (with fsync) to avoid loss of data in case of
crashes, but it is impossible to guarantee this in all cases.
*/



#ifndef MATILDA_FLOG_H
#define MATILDA_FLOG_H

#include "matilda.h"

#include "types.h"

#define LOG_CRITICAL    1
#define LOG_WARNING     2
#define LOG_PROTOCOL    4
#define LOG_INFORMATION 8
#define LOG_DEBUG       16

#if MATILDA_RELEASE_MODE
#define DEFAULT_LOG_MODES (LOG_CRITICAL | LOG_WARNING)
#else
#define DEFAULT_LOG_MODES (LOG_CRITICAL | LOG_WARNING | LOG_DEBUG)
#endif


/*
Sets the logging messages that are written to file based on a mask of the
combination of available message types. See flog.h for more information.
*/
void config_logging(
    u16 new_mode
);

/*
Set whether to also print messages to the standard error file descriptor.
(On by default)
*/
void flog_set_print_to_stderr(
    bool print
);

/*
Obtain a textual description of the capabilities and configuration options of
matilda. This mostly concerns compile time constants.
RETURNS string with build information
*/
const char * build_info();

/*
Log a message with verbosity level critical.
*/
void flog_crit(
    const char * ctx,
    const char * msg
);

/*
Log a message with verbosity level warning.
*/
void flog_warn(
    const char * ctx,
    const char * msg
);

/*
Log a message with verbosity level communication protocol.
*/
void flog_prot(
    const char * ctx,
    const char * msg
);

/*
    Log a message with verbosity level informational.
*/
void flog_info(
    const char * ctx,
    const char * msg
);

/*
    Log a message with verbosity level debug.
*/
void flog_dbug(
    const char * ctx,
    const char * msg
);

#endif
