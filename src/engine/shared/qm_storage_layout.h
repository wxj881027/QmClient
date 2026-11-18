#ifndef ENGINE_SHARED_QM_STORAGE_LAYOUT_H
#define ENGINE_SHARED_QM_STORAGE_LAYOUT_H

class IStorage;

// QmClient 用户存档的唯一布局入口。settings.cfg 单独留在 qmclient/ 根目录：
// 它同时是配置系统 v3 的迁移哨兵，移动它会改变“配置是否已收敛”的判定。
namespace qm_storage
{
	inline constexpr const char *ROOT_DIR = "qmclient";
	inline constexpr const char *SETTINGS_FILE = "qmclient/settings.cfg";

	inline constexpr const char *CONFIG_DIR = "qmclient/config";
	inline constexpr const char *CONFIG_CREDENTIALS_DIR = "qmclient/config/credentials";
	inline constexpr const char *CONFIG_DRAFTS_DIR = "qmclient/config/drafts";
	inline constexpr const char *CONFIG_PACKAGES_DIR = "qmclient/config/packages";

	inline constexpr const char *CACHE_DIR = "qmclient/cache";
	inline constexpr const char *CACHE_NETWORK_DIR = "qmclient/cache/network";
	inline constexpr const char *CACHE_REMOTE_DIR = "qmclient/cache/remote";
	inline constexpr const char *CACHE_WORKSHOP_DIR = "qmclient/cache/workshop";
	inline constexpr const char *CACHE_WORKSHOP_CATALOG_DIR = "qmclient/cache/workshop/catalogs";
	inline constexpr const char *CACHE_WORKSHOP_THUMBS_DIR = "qmclient/cache/workshop/thumbs";
	inline constexpr const char *CACHE_FONTS_DIR = "qmclient/cache/fonts";
	inline constexpr const char *CACHE_FONT_PREVIEWS_DIR = "qmclient/cache/fonts/previews";
	inline constexpr const char *CACHE_MAPS_DIR = "qmclient/cache/maps";
	inline constexpr const char *CACHE_MAP_COVERS_DIR = "qmclient/cache/maps/covers";
	inline constexpr const char *CACHE_MAP_PREVIEWS_DIR = "qmclient/cache/maps/previews";
	inline constexpr const char *CACHE_SETTINGS_DIR = "qmclient/cache/settings";
	inline constexpr const char *CACHE_SOCIAL_DIR = "qmclient/cache/social";

	inline constexpr const char *STATE_DIR = "qmclient/state";
	inline constexpr const char *STATE_MAPS_DIR = "qmclient/state/maps";
	inline constexpr const char *STATE_WORKSHOP_DIR = "qmclient/state/workshop";
	inline constexpr const char *STATE_LEGACY_DIR = "qmclient/state/legacy";

	inline constexpr const char *CONTENT_DIR = "qmclient/content";
	inline constexpr const char *CONTENT_RANK1_DIR = "qmclient/content/rank1";
	inline constexpr const char *CONTENT_RANK1_DEMOS_DIR = "qmclient/content/rank1/demos";
	inline constexpr const char *CONTENT_RANK1_GHOSTS_DIR = "qmclient/content/rank1/ghosts";
	inline constexpr const char *CONTENT_CHAT_LOG_DIR = "qmclient/content/chat_logs";
	inline constexpr const char *CONTENT_LYRICS_DIR = "qmclient/content/lyrics";
	inline constexpr const char *CONTENT_FONTS_DIR = "qmclient/content/fonts";

	inline constexpr const char *RUNTIME_DIR = "qmclient/runtime";
	inline constexpr const char *RUNTIME_EDITOR_DIR = "qmclient/runtime/editor";
	inline constexpr const char *RUNTIME_UPDATES_DIR = "qmclient/runtime/updates";

	inline constexpr const char *PROFILES_FILE = "qmclient/config/qmclient_profiles.cfg";
	inline constexpr const char *CHATBINDS_FILE = "qmclient/config/qmclient_chatbinds.cfg";
	inline constexpr const char *WARLIST_FILE = "qmclient/config/qmclient_warlist.cfg";
	inline constexpr const char *TITLE_TOKEN_FILE = "qmclient/config/credentials/title_token.txt";
	inline constexpr const char *DEVELOPER_TOKEN_FILE = "qmclient/config/credentials/developer_token.txt";
	inline constexpr const char *PLAYTIME_CLIENT_ID_FILE = "qmclient/config/credentials/playtime_client_id.txt";
	inline constexpr const char *MACHINE_ID_FILE = "qmclient/config/credentials/voice_machine_id.txt";
	inline constexpr const char *NEWS_DRAFT_FILE = "qmclient/config/drafts/news.md";
	inline constexpr const char *SPONSORS_DRAFT_FILE = "qmclient/config/drafts/sponsors.md";

	inline constexpr const char *SERVERLIST_CACHE_FILE = "qmclient/cache/network/serverlist.json";
	inline constexpr const char *CUSTOM_COMMUNITIES_CACHE_FILE = "qmclient/cache/network/custom-communities.json";
	inline constexpr const char *BROADCAST_CACHE_FILE = "qmclient/cache/remote/broadcast.json";
	inline constexpr const char *NEWS_CACHE_FILE = "qmclient/cache/remote/news.json";
	inline constexpr const char *SPONSORS_CACHE_FILE = "qmclient/cache/remote/sponsors.json";
	inline constexpr const char *WORD_FILTER_CACHE_FILE = "qmclient/cache/remote/public_word_filter.txt";
	inline constexpr const char *WORKSHOP_AUTHOR_CACHE_FILE = "qmclient/state/workshop/local_asset_authors.json";
	inline constexpr const char *FRIEND_LAST_SEEN_CACHE_FILE = "qmclient/cache/social/friend_last_seen.json";
	inline constexpr const char *MAP_DIFFICULTY_CACHE_FILE = "qmclient/cache/maps/difficulty.json";
	inline constexpr const char *GLYPH_PREWARM_FILE = "qmclient/cache/settings/glyph_prewarm.txt";
	inline constexpr const char *SETTINGS_METADATA_FILE = "qmclient/cache/settings/runtime_metadata.cfg";

	inline constexpr const char *STATISTICS_FILE = "qmclient/state/statistics.json";
	inline constexpr const char *SCORE_CACHE_FILE = "qmclient/state/score_cache.sqlite3";
	inline constexpr const char *SAVE_USAGE_FILE = "qmclient/state/legacy/save_usage.sqlite3";
	inline constexpr const char *MAP_CATEGORIES_FILE = "qmclient/state/maps/categories.json";
	inline constexpr const char *MAP_HISTORY_FILE = "qmclient/state/maps/history.json";
	inline constexpr const char *MAP_NOTES_FILE = "qmclient/state/maps/notes.json";
	inline constexpr const char *LOCAL_MAP_TAGS_FILE = "qmclient/state/maps/local_tags.txt";

	inline constexpr const char *CONFIG_SAVE_LOCK_FILE = "qmclient/runtime/config_save.lock";
	inline constexpr const char *LIFECYCLE_MARKER_FILE = "qmclient/runtime/lifecycle_pending.marker";
	inline constexpr const char *CRASH_REPORTER_MARKER_FILE = "qmclient/runtime/crash_reporter_started_at.marker";
	inline constexpr const char *GRAPHICS_RECOVERY_MARKER_FILE = "qmclient/runtime/graphics_recovery.marker";
	inline constexpr const char *EDITOR_COLLAB_SNAPSHOT_FILE = "qmclient/runtime/editor/collab_snapshot.map";
	inline constexpr const char *EDITOR_COLLAB_INCOMING_FILE = "qmclient/runtime/editor/collab_incoming.map";
	inline constexpr const char *LAYOUT_MIGRATION_MARKER_FILE = "qmclient/runtime/storage_layout_v1.done";

	bool EnsureStorageLayout(IStorage *pStorage);
	bool EnsureFolder(IStorage *pStorage, const char *pPath);
	bool StorageLayoutMigrationPending(IStorage *pStorage);
	// 只迁移明确归属 QmClient 的文件；目标已存在且内容不同时不覆盖并保留旧文件。
	// 全部旧路径处理完成后才写 marker，可安全重复调用。
	bool MigrateStorageLayout(IStorage *pStorage);
}

#endif
