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
 * It quits once the started program has a window and the focus, or after two minutes
 * (the first start creates the prefix); Wine's own windows carry on.
 *
 * Focus: since macOS 14 an app only becomes active if the active app lets it. The
 * Finder activates this launcher, not the Wine process it starts, and that process
 * asks for the focus later, when its first window appears. Wine apps announce that
 * with a distributed notification (WineAppWillActivateNotification, winemac.drv
 * cocoa_app.m) and yield to each other; the launcher listens for it too and yields
 * to the first one, so the program comes to the front as if it had been opened
 * directly.
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
@property BOOL opened, waiting;
@end

@implementation Launcher

/* a Wine process wants the focus: let it have it, then we are done */
- (void)wineAppWillActivate:(NSNotification *)note
{
    pid_t pid = [note.userInfo[@"ActivatingAppPID"] intValue];
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];

    if (!app) return;
    [NSApp yieldActivationToApplication:app];
    [app activateFromApplication:[NSRunningApplication currentApplication] options:0];
    dispatch_after( dispatch_time( DISPATCH_TIME_NOW, 500 * NSEC_PER_MSEC ), dispatch_get_main_queue(), ^{
        [NSApp terminate:nil];
    });
}

/* wait for the started program to ask for the focus, at most two minutes */
- (void)handOverFocus
{
    if (self.waiting) return;
    self.waiting = YES;
    dispatch_after( dispatch_time( DISPATCH_TIME_NOW, 120 * NSEC_PER_SEC ), dispatch_get_main_queue(), ^{
        [NSApp terminate:nil];
    });
}

- (void)application:(NSApplication *)app openURLs:(NSArray<NSURL *> *)urls
{
    self.opened = YES;
    for (NSURL *url in urls) if (url.isFileURL) start_detached( @[url.path] );
    [self handOverFocus];
}

- (void)applicationWillFinishLaunching:(NSNotification *)note
{
    /* before anything is started, so the request can't be missed */
    [[NSDistributedNotificationCenter defaultCenter] addObserver:self
                                                        selector:@selector(wineAppWillActivate:)
                                                            name:@"WineAppWillActivateNotification"
                                                          object:nil
                                              suspensionBehavior:NSNotificationSuspensionBehaviorDeliverImmediately];
}

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
    /* an "open documents" event, if any, arrives before this; give it a moment */
    dispatch_after( dispatch_time( DISPATCH_TIME_NOW, 300 * NSEC_PER_MSEC ), dispatch_get_main_queue(), ^{
        if (!self.opened) start_detached( @[] );
        [self handOverFocus];
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
