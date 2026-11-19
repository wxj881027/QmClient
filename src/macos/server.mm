#import <Cocoa/Cocoa.h>

@interface ServerView : NSTextView <NSApplicationDelegate, NSWindowDelegate>
{
	NSTask *pTask;
	NSFileHandle *pFile;
}
- (void)listenToTask:(NSTask *)pServerTask output:(NSFileHandle *)pOutput;
- (void)stopServer;
@end

@implementation ServerView
- (void)listenToTask:(NSTask *)pServerTask output:(NSFileHandle *)pOutput
{
	pTask = [pServerTask retain];
	pFile = [pOutput retain];

	[[NSNotificationCenter defaultCenter] addObserver: self selector: @selector(outputNotification:) name: NSFileHandleReadCompletionNotification object: pFile];

	[pFile readInBackgroundAndNotify];
}

- (void)stopListening
{
	if(pFile == nil)
		return;
	[[NSNotificationCenter defaultCenter] removeObserver:self name:NSFileHandleReadCompletionNotification object:pFile];
	[pFile release];
	pFile = nil;
}

- (void)dealloc
{
	[self stopListening];
	[pTask release];
	[super dealloc];
}

- (void)outputNotification:(NSNotification *)notification
{
	NSData *pData = [[notification userInfo] objectForKey:NSFileHandleNotificationDataItem];
	if([pData length] == 0)
	{
		// 管道已经结束，继续调度读取会反复收到空通知。
		[pFile closeAndReturnError:nullptr];
		[self stopListening];
		return;
	}

	NSString *pString = [[NSString alloc] initWithData:pData encoding:NSUTF8StringEncoding];
	if(pString == nil)
		pString = [[NSString alloc] initWithData:pData encoding:NSISOLatin1StringEncoding];
	if(pString != nil)
	{
		NSDictionary *pAttributes = @{
			NSForegroundColorAttributeName: [NSColor whiteColor],
			NSFontAttributeName: [NSFont monospacedSystemFontOfSize:[NSFont systemFontSize] weight:NSFontWeightRegular],
		};
		NSAttributedString *pAttrStr = [[NSAttributedString alloc] initWithString:pString attributes:pAttributes];
		NSTextStorage *pTextStorage = [self textStorage];
		[pTextStorage appendAttributedString:pAttrStr];
		[self scrollRangeToVisible:NSMakeRange([pTextStorage length], 0)];
		[pAttrStr release];
	}
	[pString release];
	[pFile readInBackgroundAndNotify];
}

- (void)windowWillClose:(NSNotification *)notification
{
	[NSApp terminate:self];
}

- (void)applicationWillTerminate:(NSNotification *)notification
{
	[self stopServer];
}

- (void)stopServer
{
	if([pTask isRunning])
		[pTask terminate];
	[self stopListening];
}
@end

static void ShowServerLaunchError(NSString *pReason)
{
	NSAlert *pAlert = [[[NSAlert alloc] init] autorelease];
	[pAlert setMessageText:@"Could not start DDNet Server"];
	[pAlert setInformativeText:pReason];
	[pAlert runModal];
}

int runServer()
{
	[NSApplication sharedApplication];
	NSArray *pArguments = [NSArray array];

	NSAlert *pAlert = [[[NSAlert alloc] init] autorelease];
	[pAlert setMessageText: @"Run DDNet Server"];
	[pAlert addButtonWithTitle: @"Use default config"];
	[pAlert addButtonWithTitle: @"Select config"];
	[pAlert addButtonWithTitle: @"Cancel"];
	switch([pAlert runModal])
	{
		case NSAlertFirstButtonReturn:
			break;
		case NSAlertThirdButtonReturn:
			return 0;
		case NSAlertSecondButtonReturn:
			// get a server config
			NSOpenPanel *pOpenDlg = [NSOpenPanel openPanel];
			[pOpenDlg setCanChooseFiles:YES];
			if([pOpenDlg runModal] != NSModalResponseOK)
				return 0;
			NSString *pFilename = [[pOpenDlg URL] path];
			pArguments = [NSArray arrayWithObjects: @"-f", pFilename, nil];
			break;
	}

	NSBundle *pMainBundle = [NSBundle mainBundle];
	NSURL *pExecutable = [pMainBundle URLForAuxiliaryExecutable:@"DDNet-Server"];
	NSURL *pResources = [pMainBundle resourceURL];
	if(pExecutable == nil || pResources == nil)
	{
		ShowServerLaunchError(@"The server executable or resource directory is missing from the app bundle.");
		return 1;
	}

	NSTask *pTask = [[[NSTask alloc] init] autorelease];
	NSPipe *pPipe = [NSPipe pipe];
	[pTask setExecutableURL:pExecutable];
	[pTask setCurrentDirectoryURL:pResources];
	[pTask setArguments:pArguments];
	[pTask setStandardOutput:pPipe];
	NSError *pLaunchError = nil;
	if(![pTask launchAndReturnError:&pLaunchError])
	{
		ShowServerLaunchError(pLaunchError != nil ? [pLaunchError localizedDescription] : @"The server process could not be launched.");
		return 1;
	}

	// run server
	NSWindow *pWindow;
	ServerView *pView;
	NSRect GraphicsRect;

	GraphicsRect = NSMakeRect(100.0, 1000.0, 600.0, 400.0);

	pWindow = [[NSWindow alloc]
		initWithContentRect: GraphicsRect
		styleMask: NSWindowStyleMaskTitled
		| NSWindowStyleMaskClosable
		| NSWindowStyleMaskMiniaturizable
		backing: NSBackingStoreBuffered
		defer: NO];

	[pWindow setReleasedWhenClosed:NO];
	[pWindow setTitle: @"DDNet Server"];

	pView = [[[ServerView alloc] initWithFrame: GraphicsRect] autorelease];
	[pView setEditable: NO];
	[pView setBackgroundColor: [NSColor blackColor]];
	[pView setRulerVisible: YES];

	[pWindow setContentView: pView];
	[pWindow setDelegate:pView];
	[NSApp setDelegate:pView];
	[pView listenToTask:pTask output:[pPipe fileHandleForReading]];
	[pWindow makeKeyAndOrderFront: nil];

	[NSApp run];
	[NSApp setDelegate:nil];
	[pView stopServer];
	[pWindow release];
	return 0;
}

#if !defined(QM_MACOS_SERVER_LAUNCHER_NO_MAIN)
int main(int argc, char **argv)
{
	@autoreleasepool
	{
		return runServer();
	}
}
#endif
