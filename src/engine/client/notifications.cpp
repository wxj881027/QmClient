#include "notifications.h"

#include <base/detect.h>

#if defined(CONF_PLATFORM_MACOS)
// Code is in src/macos/notification.mm.
void NotificationsNotifyMacOsInternal(const char *pTitle, const char *pMessage);
#elif defined(CONF_FAMILY_UNIX) && !defined(CONF_PLATFORM_ANDROID) && !defined(CONF_PLATFORM_HAIKU) && !defined(CONF_PLATFORM_EMSCRIPTEN) && !defined(CONF_PLATFORM_IOS)
#include <libnotify/notify.h>
#define NOTIFICATIONS_USE_LIBNOTIFY
#endif

void CNotifications::Init(const char *pAppname)
{
#if defined(NOTIFICATIONS_USE_LIBNOTIFY)
	notify_init(pAppname);
#endif
}

void CNotifications::Shutdown()
{
#if defined(NOTIFICATIONS_USE_LIBNOTIFY)
	notify_uninit();
#endif
}

void CNotifications::Notify(const char *pTitle, const char *pMessage)
{
#if defined(CONF_PLATFORM_MACOS)
	NotificationsNotifyMacOsInternal(pTitle, pMessage);
#elif defined(NOTIFICATIONS_USE_LIBNOTIFY)
	// 官方 13102：正文按纯文本转义，玩家名里的尖括号等标记不会被 libnotify 当富文本解析
	gchar *pEscaped = g_markup_escape_text(pMessage, -1);
	NotifyNotification *pNotif = notify_notification_new(pTitle, pEscaped, "ddnet");
	if(pNotif)
	{
		notify_notification_show(pNotif, NULL);
		g_object_unref(G_OBJECT(pNotif));
	}
	g_free(pEscaped);
#endif
}

INotifications *CreateNotifications()
{
	return new CNotifications();
}
