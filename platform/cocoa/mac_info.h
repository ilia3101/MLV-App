#ifndef _mac_info_h_
#define _mac_info_h_

/* Useful infos for MmMMmac */

/* RAM in MegaBytes */
#define MAC_RAM (int)((uint64_t)[[NSProcessInfo processInfo] physicalMemory] >> 20)

/* CPU cores, or threads (I have an i5 how would I know?) */
#define MAC_CORES ((int)[[NSProcessInfo processInfo] processorCount])

/* Screen resolution */
#define SCREEN_WIDTH ((NSRect)[[NSScreen mainScreen] frame]).size.width
#define SCREEN_HEIGHT ((NSRect)[[NSScreen mainScreen] frame]).size.height

/* To get the date */
#define CURRENT_DAY ((int)[[[NSCalendar currentCalendar] components: NSCalendarUnitDay fromDate:[NSDate date]] day])
#define CURRENT_MONTH ((int)[[[NSCalendar currentCalendar] components: NSCalendarUnitMonth fromDate:[NSDate date]] month])
#define CURRENT_YEAR ((int)[[[NSCalendar currentCalendar] components: NSCalendarUnitYear fromDate:[NSDate date]] year])

#endif