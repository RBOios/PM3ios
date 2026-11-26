#import "PM3Settings.h"

static NSString *const kFolderBookmarkKey = @"pm3.gameFolderBookmark";
static NSURL *sAccessedURL = nil;          // security-scoped URL we currently hold open (the game reads files from it while it runs)

@implementation PM3Settings

+ (NSString *)volumeKeyForChannel:(int)channel { return [NSString stringWithFormat:@"pm3.volume.%d", channel]; }

+ (float)volumeForChannel:(int)channel {
    id v = [NSUserDefaults.standardUserDefaults objectForKey:[self volumeKeyForChannel:channel]];
    if (![v isKindOfClass:NSNumber.class]) return 1.0f;
    float f = [(NSNumber *)v floatValue];
    return f < 0.0f ? 0.0f : (f > 1.5f ? 1.5f : f);
}

+ (void)setVolume:(float)volume forChannel:(int)channel {
    [NSUserDefaults.standardUserDefaults setObject:@(volume) forKey:[self volumeKeyForChannel:channel]];
}

+ (void)applyVolumesToBridge:(Pm3Bridge *)bridge {
    for (int ch = 0; ch < 3; ch++) pm3_bridge_set_volume(bridge, ch, [self volumeForChannel:ch]);
}

+ (NSString *)documentsPath {
    return NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES).firstObject;
}

+ (NSString *)savesPath {
    NSString *saves = [[self documentsPath] stringByAppendingPathComponent:@"Saves"];
    [[NSFileManager defaultManager] createDirectoryAtPath:saves withIntermediateDirectories:YES attributes:nil error:nil];
    return saves;
}

+ (BOOL)directory:(NSString *)dir containsFile:(NSString *)name {
    for (NSString *entry in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil]) {
        if ([entry caseInsensitiveCompare:name] == NSOrderedSame) return YES;
    }
    return NO;
}

+ (NSString *)findGameRootInDirectory:(NSString *)dir {
    if (dir.length == 0) return nil;
    if ([self directory:dir containsFile:@"PM3WIN.EXE"]) return dir;
    for (NSString *entry in [[NSFileManager defaultManager] contentsOfDirectoryAtPath:dir error:nil]) {
        NSString *sub = [dir stringByAppendingPathComponent:entry];
        BOOL isDir = NO;
        if ([[NSFileManager defaultManager] fileExistsAtPath:sub isDirectory:&isDir] && isDir && [self directory:sub containsFile:@"PM3WIN.EXE"]) return sub;
    }
    return nil;
}

+ (void)beginAccessToURL:(NSURL *)url {
    if (sAccessedURL) { [sAccessedURL stopAccessingSecurityScopedResource]; sAccessedURL = nil; }
    if (url && [url startAccessingSecurityScopedResource]) sAccessedURL = url;   // returns NO for folders inside our own sandbox: nothing to release then
}

+ (BOOL)rememberGameFolderURL:(NSURL *)url {
    NSError *err = nil;
    NSData *data = [url bookmarkDataWithOptions:0 includingResourceValuesForKeys:nil relativeToURL:nil error:&err];
    if (!data) { NSLog(@"PM3: could not create a bookmark for %@: %@", url, err); return NO; }
    [NSUserDefaults.standardUserDefaults setObject:data forKey:kFolderBookmarkKey];
    return YES;
}

+ (NSURL *)restoreGameFolderURL {
    NSData *data = [NSUserDefaults.standardUserDefaults dataForKey:kFolderBookmarkKey];
    if (!data) return nil;
    BOOL stale = NO; NSError *err = nil;
    NSURL *url = [NSURL URLByResolvingBookmarkData:data options:0 relativeToURL:nil bookmarkDataIsStale:&stale error:&err];
    if (!url) { NSLog(@"PM3: saved game folder cannot be resolved: %@", err); return nil; }
    [self beginAccessToURL:url];
    if (stale) [self rememberGameFolderURL:url];
    return url;
}

@end
