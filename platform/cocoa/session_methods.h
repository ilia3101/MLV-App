#ifndef _session_methods_h_
#define _session_methods_h_

#include "godobject.h"

/* Methods/functions for handling sessions */

/* This is a function as it may be used in more than one place */
void sessionAddNewMlvClip(char * mlvPathString);
/* Called from -(void)openSessionDialog - currently only loads first clip */
void appLoadSession(char * sessionPath);
/* Save current session */
void appWriteSession(char * sessionPath);
/* Frees/deletes all mlv objects */
void appClearSession();
/* Savve current GUI state to a clipinfo object */
void saveClipInfo(clipInfo_t * clip);
/* Set app GUI According to clipinfo struct */
void setAppGUIFromClip(clipInfo_t * clip);
/* Like previous, but doesn't set open the clip */
void setAppSlidersFromClip(clipInfo_t * clip);
/* Sets clip object to default GUI settings (processing parameters etc) */
void setDefaultsClip(clipInfo_t * clip);
/* Marks current clip as touched (edited) */
void setCurrentClipTouched();
/* If a clip has been changed */
BOOL isClipDefault(clipInfo_t * clip);


@interface MLVListDelegate : NSObject <NSTableViewDelegate>
/* Get label */
-(NSView *)tableView:(NSTableView *)tableView viewForTableColumn:(NSTableColumn *)tableColumn row:(NSInteger)row;
@end

@interface MLVListDataSource : NSObject <NSTableViewDataSource>
-(NSInteger)numberOfRowsInTableView:(NSTableView *)tableView;
@end

@interface NSTableView (sessionMethods)
/* When a clip is double clicked in the table view */
-(void)doubleClickSetClip;
@end

#endif
