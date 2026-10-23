/*
 * Dynamic sync control driver definitions (V2)
 *
 * by andip71 (alias Lord Boeffla)
 *
 * sdm845 changes by pappschlumpf (Erik Müller)
 */

#ifndef _LINUX_DYN_SYNC_CNTRL_H
#define _LINUX_DYN_SYNC_CNTRL_H

#define DYN_FSYNC_ACTIVE_DEFAULT true
#define DYN_FSYNC_VERSION_MAJOR 2
#define DYN_FSYNC_VERSION_MINOR 2

extern bool suspend_active;
extern bool dyn_fsync_active;

void dyn_fsync_force_flush(void);
void sync_filesystems(int wait);

#endif /* _LINUX_DYN_SYNC_CNTRL_H */
