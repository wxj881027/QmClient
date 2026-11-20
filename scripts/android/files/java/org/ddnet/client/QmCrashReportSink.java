package org.ddnet.client;

import android.content.ContentUris;
import android.content.ContentValues;
import android.content.Context;
import android.content.SharedPreferences;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.ParcelFileDescriptor;
import android.provider.MediaStore;
import android.system.Os;
import android.system.OsConstants;
import android.util.Log;

import java.io.FileDescriptor;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;

// 让崩溃报告在崩溃瞬间就落到「下载/QmClient」，不必等下一次启动：
// 信号处理器里不能调 MediaStore（ContentResolver 走 binder，JNI 也不是
// async-signal-safe），所以由进程健康时先建好条目，把裸 fd 交给 native，
// 处理器只做 ftruncate/write/fsync。
//
// 条目创建时即对用户可见，不能设 IS_PENDING：崩溃后没有人能清这个标记，
// 设了就永远看不见。代价是运行期间目录里会有一个占位文件，正常退出时删除。
final class QmCrashReportSink
{
	private static final String TAG = "QmCrashReportSink";
	private static final String DIRECTORY = Environment.DIRECTORY_DOWNLOADS + "/QmClient";
	private static final String PREFS = "qm_crash_report";
	private static final String KEY_LAST = "last_prepared";
	// 信号处理器改写过的报告以此开头，占位文本则是 "QmClient crash report"。
	private static final String FATAL_PREFIX = "QmClient fatal crash report";
	// 进程被 SIGKILL（LMK、ART OOM）时处理器不运行，留下的文件必须能自解释。
	private static final String PLACEHOLDER =
		"QmClient crash report\n" +
		"Status: the client is running; no fatal signal has been captured.\n" +
		"This file is deleted when the client exits normally.\n";
	private static final byte[] PLACEHOLDER_BYTES = PLACEHOLDER.getBytes(StandardCharsets.UTF_8);

	private static Uri s_uri;
	private static int s_fd = -1;

	private QmCrashReportSink()
	{
	}

	// 返回预打开的可写 fd；失败返回 -1，由调用方回退到沙盒报告 + 下次启动搬运。
	static synchronized int prepare(Context context, String displayName)
	{
		if(s_fd >= 0)
			return s_fd;
		if(Build.VERSION.SDK_INT < Build.VERSION_CODES.Q)
			return -1;

		ParcelFileDescriptor descriptor = null;
		try
		{
			ContentValues values = new ContentValues();
			values.put(MediaStore.MediaColumns.DISPLAY_NAME, displayName);
			values.put(MediaStore.MediaColumns.MIME_TYPE, "text/plain");
			values.put(MediaStore.MediaColumns.RELATIVE_PATH, DIRECTORY);
			final Uri uri = context.getContentResolver().insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
			if(uri == null)
			{
				Log.w(TAG, "insert returned no uri");
				return -1;
			}
			// 先登记 uri：后续任何失败都能由 discard 把这条空条目一并删掉。
			s_uri = uri;
			descriptor = context.getContentResolver().openFileDescriptor(uri, "w");
			if(descriptor == null)
			{
				Log.w(TAG, "openFileDescriptor failed");
				discard(context);
				return -1;
			}
			// 直接用 Os 写占位：FileOutputStream 关闭时会连带关闭这个 fd。
			final FileDescriptor fd = descriptor.getFileDescriptor();
			Os.lseek(fd, 0, OsConstants.SEEK_SET);
			if(Os.write(fd, PLACEHOLDER_BYTES, 0, PLACEHOLDER_BYTES.length) != PLACEHOLDER_BYTES.length)
				throw new IOException("incomplete placeholder write");
			Os.fsync(fd);
			// detachFd 之后 fd 的所有权归 native，ParcelFileDescriptor 不能再 close。
			s_fd = descriptor.detachFd();
			descriptor = null;
			// 同步落盘：崩溃处理器改写报告后，下次启动要靠这条记录找到它。
			context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
				.putString(KEY_LAST, displayName).commit();
			return s_fd;
		}
		catch(Exception error)
		{
			Log.w(TAG, "prepare failed", error);
			if(descriptor != null)
			{
				try
				{
					descriptor.close();
				}
				catch(IOException ignored)
				{
				}
			}
			discard(context);
			return -1;
		}
	}

	// 取上次启动记录的文件名并清除记录。只读 SharedPreferences，可在
	// super.onCreate 之前调用——之后 SDL 主线程会写入本次预建的条目名。
	static synchronized String takePreviousReportName(Context context)
	{
		final SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
		final String name = prefs.getString(KEY_LAST, null);
		if(name == null)
			return null;
		prefs.edit().remove(KEY_LAST).commit();
		return name;
	}

	// 条目仍在、且内容已被信号处理器改写时返回文件名，说明上次是崩溃退出。
	// 会被 MediaStore 查询阻塞，调用方应放在后台线程。
	static String confirmCrashReport(Context context, String displayName)
	{
		if(displayName == null || Build.VERSION.SDK_INT < Build.VERSION_CODES.Q)
			return null;
		final Uri uri = findDownload(context, displayName);
		if(uri == null)
			return null;
		return inspectPreviousReport(context, uri) ? displayName : null;
	}

	private static Uri findDownload(Context context, String displayName)
	{
		final String[] projection = {MediaStore.MediaColumns._ID};
		final String selection = MediaStore.MediaColumns.RELATIVE_PATH + "=? AND " + MediaStore.MediaColumns.DISPLAY_NAME + "=?";
		final String[] args = {DIRECTORY + "/", displayName};
		try(Cursor cursor = context.getContentResolver().query(
				MediaStore.Downloads.EXTERNAL_CONTENT_URI, projection, selection, args, null))
		{
			if(cursor == null || !cursor.moveToFirst())
				return null;
			return ContentUris.withAppendedId(MediaStore.Downloads.EXTERNAL_CONTENT_URI, cursor.getLong(0));
		}
		catch(RuntimeException error)
		{
			Log.w(TAG, "query failed", error);
			return null;
		}
	}

	private static synchronized boolean inspectPreviousReport(Context context, Uri uri)
	{
		if(uri.equals(s_uri))
			return false;
		boolean placeholder;
		try(InputStream in = context.getContentResolver().openInputStream(uri))
		{
			if(in == null)
				return false;
			final byte[] head = new byte[PLACEHOLDER_BYTES.length];
			int read = 0;
			while(read < head.length)
			{
				final int step = in.read(head, read, head.length - read);
				if(step <= 0)
					break;
				read += step;
			}
			if(read >= FATAL_PREFIX.length() && FATAL_PREFIX.equals(new String(head, 0, FATAL_PREFIX.length(), StandardCharsets.US_ASCII)))
				return true;
			placeholder = read == head.length && java.util.Arrays.equals(head, PLACEHOLDER_BYTES) && in.read() == -1;
		}
		catch(IOException | RuntimeException error)
		{
			Log.w(TAG, "read failed", error);
			return false;
		}
		if(placeholder)
		{
			try
			{
				context.getContentResolver().delete(uri, null, null);
			}
			catch(RuntimeException error)
			{
				Log.w(TAG, "stale placeholder delete failed", error);
			}
		}
		return false;
	}

	static synchronized void discard(Context context)
	{
		// fd 由 native 持有，随进程退出释放；这里只删除媒体库条目。
		if(s_uri != null && context != null)
		{
			try
			{
				context.getContentResolver().delete(s_uri, null, null);
			}
			catch(RuntimeException error)
			{
				Log.w(TAG, "delete failed", error);
			}
		}
		s_uri = null;
		s_fd = -1;
	}
}
