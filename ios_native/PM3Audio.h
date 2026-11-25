// UNTESTED on a device/Xcode (written without Apple SDKs). Plays what the guest wrote to waveOut: an AVAudioSourceNode pulls 44.1 kHz stereo from
// pm3_bridge_pull_audio() (the bridge converts/mixes the game's PCM). MIDI background music is NOT handled here (not implemented yet).
#import <Foundation/Foundation.h>
#include "pm3/bridge.h"

@interface PM3Audio : NSObject
- (instancetype)initWithBridge:(Pm3Bridge *)bridge;
- (void)start;
- (void)stop;
@end
