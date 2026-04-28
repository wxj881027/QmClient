#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <UserNotifications/UserNotifications.h>

static void SendNotificationRequest(UNUserNotificationCenter *pCenter, NSString *pTitle, NSString *pMessage)
{
	UNMutableNotificationContent *pContent = [[[UNMutableNotificationContent alloc] init] autorelease];
	pContent.title = pTitle;
	pContent.body = pMessage;
	pContent.sound = [UNNotificationSound defaultSound];

	NSString *pIdentifier = [[NSUUID UUID] UUIDString];
	UNTimeIntervalNotificationTrigger *pTrigger = [UNTimeIntervalNotificationTrigger triggerWithTimeInterval:0.1 repeats:NO];
	UNNotificationRequest *pRequest = [UNNotificationRequest requestWithIdentifier:pIdentifier content:pContent trigger:pTrigger];
	[pCenter addNotificationRequest:pRequest withCompletionHandler:nil];
}

void NotificationsNotifyMacOsInternal(const char *pTitle, const char *pMessage)
{
	NSString *pNsTitle = [NSString stringWithCString:pTitle encoding:NSUTF8StringEncoding];
	NSString *pNsMsg = [NSString stringWithCString:pMessage encoding:NSUTF8StringEncoding];
	UNUserNotificationCenter *pCenter = [UNUserNotificationCenter currentNotificationCenter];

	[pCenter getNotificationSettingsWithCompletionHandler:^(UNNotificationSettings *pSettings) {
		switch(pSettings.authorizationStatus)
		{
		case UNAuthorizationStatusAuthorized:
		case UNAuthorizationStatusProvisional:
			SendNotificationRequest(pCenter, pNsTitle, pNsMsg);
			break;
		case UNAuthorizationStatusNotDetermined:
			[pCenter requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
				completionHandler:^(BOOL Granted, NSError *pError) {
					(void)pError;
					if(Granted)
						SendNotificationRequest(pCenter, pNsTitle, pNsMsg);
				}];
			break;
		case UNAuthorizationStatusDenied:
			break;
		}
	}];

	[NSApp requestUserAttention:NSInformationalRequest]; // use NSCriticalRequest to annoy the user (doesn't stop bouncing)
}
