// UNTESTED (written without Xcode available). Persistent app settings: volume per sound group and the game folder the user picked.
#import <Foundation/Foundation.h>
#include "pm3/bridge.h"

@interface PM3Settings : NSObject

// Volume per sound group: channel = PM3_VOL_VOICE (0) / PM3_VOL_BGM (1) / PM3_VOL_SFX (2). Range 0..1.5, default 1.0 (the tuned loudness of the original game).
+ (float)volumeForChannel:(int)channel;
+ (void)setVolume:(float)volume forChannel:(int)channel;
+ (void)applyVolumesToBridge:(Pm3Bridge *)bridge;

+ (NSString *)documentsPath;
+ (NSString *)savesPath;                       // Documents/Saves (created on demand): everything the game writes

// PM3WIN.EXE must be in `dir` or in one of its direct sub folders (name compared case-insensitively). Returns that folder or nil.
+ (NSString *)findGameRootInDirectory:(NSString *)dir;

// The folder chosen with the document picker is remembered as a bookmark (the picker's security-scoped URL is only valid for one run otherwise).
+ (NSURL *)restoreGameFolderURL;               // resolves the saved bookmark and starts security-scoped access; nil if none / not resolvable
+ (BOOL)rememberGameFolderURL:(NSURL *)url;    // call while access to `url` is active
+ (void)beginAccessToURL:(NSURL *)url;         // stops the previous access, starts this one (kept until the next call / app exit)
@end
