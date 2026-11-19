#import <Cocoa/Cocoa.h>

#include <gtest/gtest.h>

// 链接以 QM_MACOS_SERVER_LAUNCHER_NO_MAIN 编译的 src/macos/server.mm。
@interface ServerView : NSTextView <NSApplicationDelegate>
- (void)listenToTask:(NSTask *)pServerTask output:(NSFileHandle *)pOutput;
@end

@interface QmServerReadProbe : NSObject
{
@public
	int m_ReadRequests;
	int m_CloseRequests;
}
- (void)readInBackgroundAndNotify;
- (BOOL)closeAndReturnError:(NSError **)ppError;
@end

@implementation QmServerReadProbe
- (void)readInBackgroundAndNotify
{
	++m_ReadRequests;
}
- (BOOL)closeAndReturnError:(NSError **)ppError
{
	(void)ppError;
	++m_CloseRequests;
	return YES;
}
@end

@interface QmServerTaskProbe : NSObject
{
@public
	BOOL m_Running;
	int m_TerminateRequests;
}
@end

@implementation QmServerTaskProbe
- (BOOL)isRunning
{
	return m_Running;
}
- (void)terminate
{
	++m_TerminateRequests;
	m_Running = NO;
}
@end

namespace
{
	ServerView *CreateListeningView(QmServerReadProbe *pOutput, NSTask *pTask = nil)
	{
		[NSApplication sharedApplication];
		ServerView *pView = [[[ServerView alloc] initWithFrame:NSMakeRect(0, 0, 320, 200)] autorelease];
		[pView listenToTask:pTask output:(NSFileHandle *)pOutput];
		return pView;
	}

	void PostOutput(QmServerReadProbe *pOutput, NSData *pData)
	{
		[[NSNotificationCenter defaultCenter] postNotificationName:NSFileHandleReadCompletionNotification
			object:pOutput userInfo:@{NSFileHandleNotificationDataItem: pData}];
	}
} // namespace

TEST(MacOsServerOutput, EofClosesPipeAndStopsReading)
{
	@autoreleasepool
	{
		QmServerReadProbe *pOutput = [[[QmServerReadProbe alloc] init] autorelease];
		ServerView *pView = CreateListeningView(pOutput);
		ASSERT_EQ(pOutput->m_ReadRequests, 1);

		PostOutput(pOutput, [NSData data]);
		PostOutput(pOutput, [@"late" dataUsingEncoding:NSUTF8StringEncoding]);

		EXPECT_EQ(pOutput->m_ReadRequests, 1);
		EXPECT_EQ(pOutput->m_CloseRequests, 1);
		EXPECT_EQ([[pView string] length], 0u);
	}
}

TEST(MacOsServerOutput, Utf8OutputIsDisplayedAndReadingContinues)
{
	@autoreleasepool
	{
		QmServerReadProbe *pOutput = [[[QmServerReadProbe alloc] init] autorelease];
		ServerView *pView = CreateListeningView(pOutput);
		NSString *pLine = @"玩家已连接\n";

		PostOutput(pOutput, [pLine dataUsingEncoding:NSUTF8StringEncoding]);

		EXPECT_TRUE([[pView string] isEqualToString:pLine]);
		EXPECT_EQ(pOutput->m_ReadRequests, 2);
		EXPECT_EQ(pOutput->m_CloseRequests, 0);
	}
}

TEST(MacOsServerOutput, AppQuitStopsServerAndDetachesOutput)
{
	@autoreleasepool
	{
		QmServerReadProbe *pOutput = [[[QmServerReadProbe alloc] init] autorelease];
		QmServerTaskProbe *pTask = [[[QmServerTaskProbe alloc] init] autorelease];
		pTask->m_Running = YES;
		ServerView *pView = CreateListeningView(pOutput, (NSTask *)pTask);
		NSNotification *pEvent = [NSNotification notificationWithName:NSApplicationWillTerminateNotification object:NSApp];

		[pView applicationWillTerminate:pEvent];
		PostOutput(pOutput, [@"late" dataUsingEncoding:NSUTF8StringEncoding]);

		EXPECT_EQ(pTask->m_TerminateRequests, 1);
		EXPECT_EQ(pOutput->m_ReadRequests, 1);
		EXPECT_EQ([[pView string] length], 0u);
	}
}
