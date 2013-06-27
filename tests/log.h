#ifndef _H_LOG
#define _H_LOG

extern int log_open(const char *name, int append);
extern void log_close(void);
extern void log_write(const char *fmt, ...);
extern void log_writeln(const char *str);

#endif /* _H_LOG */

/* vim: set ts=4 sw=4 sts=4 expandtab: */
