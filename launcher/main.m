/*
 * Barrique.app/Contents/MacOS/Barrique: the app's native entry point.
 *
 * The Finder hands files to an app with an Apple Event ("open documents"), not on
 * the command line, so a shell script can't be the bundle executable. This one:
 *  - started from a terminal with arguments (barrique program.exe, barrique --wine
 *    winecfg): runs Contents/Resources/barrique.sh with them, in the foreground;
 *  - opened with files (Open With, drag onto the icon): starts each one through
 *    barrique.sh, detached, output to the log in the data folder;
 *  - opened with nothing (double click): starts Wine's file manager the same way.
 * It quits once the programs are started; Wine's own windows carry on.
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 2.1 of the License, or (at your option)
 * any later version.
 */
#import <Cocoa/Cocoa.h>
#include <unistd.h>

static NSString *script_path(void)
{
    return [[NSBundle mainBundle] pathForResource:@"barrique" ofType:@"sh"];
}

static NSString *log_path(void)
{
    const char *data = getenv( "BARRIQUE_DATA" );
    NSString *dir = data ? @(data)
                         : [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/Barrique"];
    dir = [dir stringByAppendingPathComponent:@"logs"];
    [[NSFileManager defaultManager] createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:NULL];
    return [dir stringByAppendingPathComponent:@"barrique.log"];
}

/* run barrique.sh detached, its output appended to the log */
static void start_detached( NSArray<NSString *> *args )
{
    NSTask *task = [[NSTask alloc] init];
    NSString *log = log_path();
    NSFileHandle *out;

    if (![[NSFileManager defaultManager] fileExistsAtPath:log])
        [[NSFileManager defaultManager] createFileAtPath:log contents:nil attributes:nil];
    out = [NSFileHandle fileHandleForWritingAtPath:log];
    [out seekToEndOfFile];
    task.executableURL = [NSURL fileURLWithPath:@"/bin/zsh"];
    task.arguments = [@[script_path()] arrayByAddingObjectsFromArray:args];
    task.standardOutput = out;
    task.standardError = out;
    task.standardInput = [NSFileHandle fileHandleWithNullDevice];
    [task launchAndReturnError:NULL];
}

@interface Launcher : NSObject <NSApplicationDelegate>
@property BOOL opened;
@end

@implementation Launcher

- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls
{
    self.opened = YES;
    for (NSURL *url in urls) if (url.isFileURL) start_detached( @[url.path] );
    [app terminate:nil];
}

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    /* an "open documents" event, if any, arrives before this; give it a moment */
    dispatch_after( dispatch_time( DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC ), dispatch_get_main_queue(), ^{
        if (!self.opened) start_detached( @[] );
        [NSApp terminate:nil];
    });
}

@end

int main( int argc, char **argv )
{
    /* from a terminal: behave like the script, in the foreground */
    if (argc > 1 && strncmp( argv[1], "-psn_", 5 ))
    {
        NSString *script = script_path();
        char **args = calloc( argc + 2, sizeof(*args) );
        int i;

        args[0] = "/bin/zsh";
        args[1] = (char *)script.fileSystemRepresentation;
        for (i = 1; i < argc; i++) args[i + 1] = argv[i];
        execv( "/bin/zsh", args );
        perror( "execv" );
        return 1;
    }
    @autoreleasepool
    {
        NSApplication *app = [NSApplication sharedApplication];
        Launcher *delegate = [[Launcher alloc] init];
        app.delegate = delegate;
        [app run];
    }
    return 0;
}
