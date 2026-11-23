#import <Foundation/Foundation.h>
#include "platform.h"

std::string pm3_default_data_dir() {
    NSArray* paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
    if (paths.count == 0) return ".";
    return std::string([[paths objectAtIndex:0] UTF8String]);
}
