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

#define LOG_MODE_ERROR  1
#define LOG_MODE_WARN   2
#define LOG_MODE_PROT   4
#define LOG_MODE_INFO   8
#define LOG_MODE_DEBUG 16

#define LOG_DEST_STDOUT 1
#define LOG_DEST_STDERR 2
#define LOG_DEST_FILE   4

#if MATILDA_RELEASE_MODE
#define DEFAULT_LOG_MODES (LOG_MODE_ERROR | LOG_MODE_WARN)
#else
#define DEFAULT_LOG_MODES (LOG_MODE_ERROR | LOG_MODE_WARN | LOG_MODE_DEBUG)
#endif

#define DEFAULT_LOG_DESTS (LOG_DEST_STDERR | LOG_DEST_FILE)

/*
Sets the logging messages that are written to file based on a mask of the
combination of available message types. See flog.h for more information.
*/
void flog_config_modes(
    u16 new_mode
);

/*
    Define the destinations for logging.
*/
void flog_config_destinations(
    u16 new_dest
);

/*
Obtain a textual description of the capabilities and configuration options of
matilda. This mostly concerns compile time constants.
RETURNS string with build information
*/
void build_info(
    char * dst
);

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
