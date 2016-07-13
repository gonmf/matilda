/*
Support for logging to file. Logging is made to a file called matilda-XXXXXX.log
where XXXXXX is a random string. When logging a logging level is set which
specifies the degree of detail of the messages to be written to file. Having a
very high degree of detail in very fast matches actively hurts the performance.

Writing to files is synchronous (with fsync) to avoid loss of data in case of
crashes, but it is impossible to guarantee this in all cases.
*/


#ifndef MATILDA_FLOG_H
#define MATILDA_FLOG_H

#include "matilda.h"

#include "types.h"

#define LOG_NONE     0
#define LOG_CRITICAL 1
#define LOG_GTP_WARN 2
#define LOG_INFORM   3

#define DEFAULT_LOG_LVL LOG_CRITICAL


/*
Set logging level of messages to actually write to file.
Messages more verbose than the level specified are ignored.
*/
void set_logging_level(
    u8 lvl
);

/*
Obtain a textual description of the capabilities and configuration options of
matilda. This mostly concerns compile time constants.
RETURNS head allocated string with build information
*/
const char * build_info();

/*
Logs the build information to file (level: informational).
*/
void flog_build_info();

/*
Log a message with verbosity level critical.
*/
void flog_crit(
    const char * s
);


/*
Log a message with verbosity level warning.
*/
void flog_warn(
    const char * s
);


/*
Log a message with verbosity level protocolar.
*/
void flog_prot(
    const char * s
);


/*
    Log a message with verbosity level informational.
*/
void flog_info(
    const char * s
);

#endif
