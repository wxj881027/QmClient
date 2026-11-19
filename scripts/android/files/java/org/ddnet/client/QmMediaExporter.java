package org.ddnet.client;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ClipData;
import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Intent;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.MediaStore;
import android.util.Log;
import android.widget.Toast;

import androidx.core.content.FileProvider;
import com.qmclient.client.R;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InterruptedIOException;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Locale;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class QmMediaExporter {
	private static final int REQUEST_EXPORT = 0x514d;
	private final Activity activity;
	private ExecutorService worker;
	private final Set<String> excludedPaths = new HashSet<>();
	private File root;
	private boolean videos;
	private String pendingPath;
	private volatile boolean busy;
	private AlertDialog dialog;
	private volatile boolean closed;

	public QmMediaExporter(Activity activity, Bundle savedState) {
		this.activity = activity;
		if(savedState != null) {
			pendingPath = savedState.getString("qm.media.export.path");
			busy = pendingPath != null;
		}
	}

	public void saveState(Bundle state) {
		if(pendingPath != null)
			state.putString("qm.media.export.path", pendingPath);
	}

	public void close() {
		closed = true;
		try {
			if(worker != null)
				worker.shutdownNow();
		} catch(RuntimeException | OutOfMemoryError ignored) {
		} finally {
			clearDialog();
		}
	}

	private void notifyUser(int message) {
		try {
			if(!closed && !activity.isFinishing() && !activity.isDestroyed())
				Toast.makeText(activity, message, Toast.LENGTH_LONG).show();
		} catch(RuntimeException | OutOfMemoryError ignored) {
			// 失败提示不能覆盖已经清理的操作状态。
		}
	}

	// 创建、提交和关闭均由Activity主线程调用；构造异常交给提交方的失败出口。
	private ExecutorService getWorker() {
		if(closed)
			throw new IllegalStateException("Media exporter is closed");
		if(worker == null)
			worker = Executors.newSingleThreadExecutor();
		return worker;
	}

	private void clearDialog() {
		AlertDialog current = dialog;
		dialog = null;
		try {
			if(current != null)
				current.dismiss();
		} catch(RuntimeException | OutOfMemoryError ignored) {
		}
	}

	public void browse(String directory, boolean videos, String[] excluded) {
		if(busy || dialog != null || closed || activity.isFinishing() || activity.isDestroyed())
			return;
		try {
			root = new File(directory).getCanonicalFile();
			this.videos = videos;
			excludedPaths.clear();
			for(String path : excluded)
				excludedPaths.add(new File(path).getCanonicalPath());
			showDirectory(root);
		} catch(IOException | RuntimeException | OutOfMemoryError error) {
			clearDialog();
			notifyUser(R.string.qm_media_failed);
		}
	}

	private boolean allowed(File file) throws IOException {
		return QmMediaFiles.contains(root, file) && !excludedPaths.contains(file.getCanonicalPath());
	}

	private void showDirectory(File directory) throws IOException {
		if(!allowed(directory))
			throw new IOException("Outside media directory");
		File[] files = directory.listFiles();
		if(files == null) {
			notifyUser(directory.exists() ? R.string.qm_media_failed : R.string.qm_media_empty);
			return;
		}
		Arrays.sort(files, (left, right) -> {
			if(left.isDirectory() != right.isDirectory())
				return left.isDirectory() ? -1 : 1;
			return left.getName().compareToIgnoreCase(right.getName());
		});
		ArrayList<File> entries = new ArrayList<>();
		ArrayList<String> names = new ArrayList<>();
		if(!directory.equals(root)) {
			entries.add(directory.getParentFile());
			names.add("../");
		}
		for(File file : files) {
			if(allowed(file) && (file.isDirectory() || (file.isFile() && QmMediaFiles.matches(file, videos)))) {
				entries.add(file);
				names.add(file.getName() + (file.isDirectory() ? "/" : ""));
			}
		}
		if(entries.isEmpty()) {
			notifyUser(R.string.qm_media_empty);
			return;
		}
		dialog = new AlertDialog.Builder(activity)
			.setTitle(videos ? R.string.qm_media_videos : R.string.qm_media_demos)
			.setItems(names.toArray(new String[0]), (sender, index) -> {
				dialog = null;
				if(closed)
					return;
				try {
					File selected = entries.get(index);
					if(!allowed(selected))
						throw new IOException("Invalid selection");
					if(selected.isDirectory())
						showDirectory(selected);
					else
						showActions(selected);
				} catch(IOException | RuntimeException | OutOfMemoryError error) {
					clearDialog();
					notifyUser(R.string.qm_media_failed);
				}
			})
			.setNegativeButton(android.R.string.cancel, null)
			.create();
		AlertDialog current = dialog;
		current.setOnDismissListener(sender -> { if(dialog == current) dialog = null; });
		current.show();
	}

	private void showActions(File file) {
		CharSequence[] actions = {activity.getText(R.string.qm_media_export), activity.getText(R.string.qm_media_share)};
		dialog = new AlertDialog.Builder(activity)
			.setTitle(file.getName())
			.setItems(actions, (sender, index) -> {
				dialog = null;
				if(closed)
					return;
				if(index == 0)
					export(file);
				else
					share(file);
			})
			.setNegativeButton(android.R.string.cancel, null)
			.create();
		AlertDialog current = dialog;
		current.setOnDismissListener(sender -> { if(dialog == current) dialog = null; });
		current.show();
	}

	private String mimeType(File file) {
		return QmMediaFiles.matches(file, true) ? "video/mp4" : "application/octet-stream";
	}

	// 复制到公共下载目录：Android 10+ 用 MediaStore.Downloads（不需要存储权限、
	// 文件管理器和「下载」应用都能看到），旧版本退回公共 Downloads 直写。
	// 返回最终文件名，失败抛 IOException。
	// 崩溃报告是纯文本，用 text/plain 才能被文件管理器直接预览；其余保持二进制类型。
	public static String copyToDownloads(Activity activity, File source) throws IOException {
		final String displayName = source.getName();
		final String mime = displayName.toLowerCase(Locale.ROOT).endsWith(".txt") ? "text/plain" : "application/octet-stream";
		if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
			final ContentResolver resolver = activity.getContentResolver();
			copyDownload(source, new DownloadTarget() {
				private Uri uri;

				@Override
				public OutputStream open() throws IOException {
					ContentValues values = new ContentValues();
					values.put(MediaStore.MediaColumns.DISPLAY_NAME, displayName);
					values.put(MediaStore.MediaColumns.MIME_TYPE, mime);
					values.put(MediaStore.MediaColumns.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS + "/QmClient");
					values.put(MediaStore.MediaColumns.IS_PENDING, 1);
					uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
					if(uri == null)
						throw new IOException("insert failed");
					return resolver.openOutputStream(uri);
				}

				@Override
				public boolean publish() {
					ContentValues values = new ContentValues();
					values.put(MediaStore.MediaColumns.IS_PENDING, 0);
					return resolver.update(uri, values, null, null) > 0;
				}

				@Override
				public void discard() {
					if(uri != null)
						resolver.delete(uri, null, null);
				}
			});
			return displayName;
		}
		File dir = new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS), "QmClient");
		return copyToLegacyDownloads(dir, source);
	}

	interface DownloadTarget {
		OutputStream open() throws IOException;
		boolean publish();
		void discard();
	}

	static void copyDownload(File source, DownloadTarget target) throws IOException {
		boolean published = false;
		try {
			try(FileInputStream input = new FileInputStream(source); OutputStream output = target.open()) {
				if(output == null)
					throw new IOException("open failed");
				QmMediaFiles.copy(input, output);
			}
			if(!target.publish())
				throw new IOException("publish failed");
			published = true;
		} finally {
			if(!published) {
				try { target.discard(); }
				catch(RuntimeException | OutOfMemoryError error) { logFailure("Download cleanup failed", error); }
			}
		}
	}

	static String copyToLegacyDownloads(File dir, File source) throws IOException {
		if(!dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory())
			throw new IOException("mkdir failed");
		final String displayName = source.getName();
		final int dot = displayName.lastIndexOf('.');
		final String base = dot > 0 ? displayName.substring(0, dot) : displayName;
		final String ext = dot > 0 ? displayName.substring(dot) : "";
		File target = new File(dir, displayName);
		int index = 1;
		while(!target.createNewFile()) {
			target = new File(dir, base + "_" + index + ext);
			index++;
		}
		boolean complete = false;
		try {
			try(FileInputStream input = new FileInputStream(source); OutputStream output = new FileOutputStream(target)) {
				QmMediaFiles.copy(input, output);
			}
			complete = true;
			return target.getName();
		} finally {
			if(!complete) {
				try { target.delete(); }
				catch(RuntimeException | OutOfMemoryError error) { logFailure("Download cleanup failed", error); }
			}
		}
	}

	private void export(File file) {
		if(closed || busy)
			return;
		try {
			pendingPath = file.getAbsolutePath();
			busy = true;
			Intent intent = new Intent(Intent.ACTION_CREATE_DOCUMENT)
				.addCategory(Intent.CATEGORY_OPENABLE)
				.setType(mimeType(file))
				.putExtra(Intent.EXTRA_TITLE, file.getName());
			activity.startActivityForResult(intent, REQUEST_EXPORT);
		} catch(RuntimeException | OutOfMemoryError error) {
			pendingPath = null;
			busy = false;
			notifyUser(R.string.qm_media_failed);
		}
	}

	public boolean onActivityResult(int requestCode, int resultCode, Intent data) {
		if(requestCode != REQUEST_EXPORT)
			return false;
		if(closed)
			return true;
		String source = pendingPath;
		pendingPath = null;
		if(resultCode != Activity.RESULT_OK || source == null || data == null || data.getData() == null) {
			busy = false;
			return true;
		}
		Uri target = data.getData();
		notifyUser(R.string.qm_media_copying);
		try {
			getWorker().execute(() -> {
				boolean success = false;
				try {
					checkWorkerOpen();
					try(FileInputStream input = new FileInputStream(source);
						OutputStream output = activity.getContentResolver().openOutputStream(target, "w")) {
						if(output == null)
							throw new IOException("No output stream");
						QmMediaFiles.copy(input, output);
					}
					checkWorkerOpen();
					success = true;
				} catch(IOException | RuntimeException | OutOfMemoryError error) {
					// 选择器也会返回用户确认覆盖的原文档。写入失败后保留该文档。
					logFailure("Export failed", error);
				}
				finishWorker(success, null, null);
			});
		} catch(RuntimeException | OutOfMemoryError error) {
			busy = false;
			logFailure("Export submission failed", error);
			notifyUser(R.string.qm_media_failed);
		}
		return true;
	}

	private void share(File source) {
		if(closed || busy)
			return;
		busy = true;
		notifyUser(R.string.qm_media_copying);
		try {
			getWorker().execute(() -> {
				File shareDirectory = null;
				File staged = null;
				boolean success = false;
				Uri uri = null;
				try {
					checkWorkerOpen();
					File directory = new File(activity.getCacheDir(), "qm-media/" + UUID.randomUUID());
					if(!directory.mkdirs())
						throw new IOException("Could not prepare share directory");
					shareDirectory = directory;
					staged = new File(directory, source.getName());
					try(FileInputStream input = new FileInputStream(source); FileOutputStream output = new FileOutputStream(staged)) {
						QmMediaFiles.copy(input, output);
					}
					uri = FileProvider.getUriForFile(activity, activity.getPackageName() + ".qm.media", staged);
					checkWorkerOpen();
					success = true;
				} catch(IOException | RuntimeException | OutOfMemoryError error) {
					discardShare(staged, shareDirectory);
					logFailure("Share failed", error);
				}
				finishWorker(success, success ? staged : null, uri);
			});
		} catch(RuntimeException | OutOfMemoryError error) {
			busy = false;
			logFailure("Share submission failed", error);
			notifyUser(R.string.qm_media_failed);
		}
	}

	private void checkWorkerOpen() throws InterruptedIOException {
		if(closed || Thread.currentThread().isInterrupted())
			throw new InterruptedIOException();
	}

	private static void logFailure(String message, Throwable error) {
		try { Log.w("QmMediaExporter", message, error); }
		catch(RuntimeException | OutOfMemoryError ignored) { }
	}

	private static void discardShare(File staged, File directory) {
		try {
			if(staged != null) {
				staged.delete();
				if(directory == null)
					directory = staged.getParentFile();
			}
			if(directory != null)
				directory.delete();
		} catch(RuntimeException | OutOfMemoryError error) {
			logFailure("Share cleanup failed", error);
		}
	}

	private void finishWorker(boolean completed, File staged, Uri uri) {
		try {
			activity.runOnUiThread(() -> {
				busy = false;
				boolean shared = false;
				try {
					if(closed || activity.isFinishing() || activity.isDestroyed())
						return;
					if(!completed) {
						notifyUser(R.string.qm_media_failed);
					} else if(uri == null) {
						notifyUser(R.string.qm_media_exported);
					} else {
						Intent intent = new Intent(Intent.ACTION_SEND).setType(mimeType(staged))
							.putExtra(Intent.EXTRA_STREAM, uri);
						intent.setClipData(ClipData.newRawUri("media", uri));
						intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
						activity.startActivity(Intent.createChooser(intent, activity.getText(R.string.qm_media_share)));
						shared = true;
					}
				} catch(RuntimeException | OutOfMemoryError error) {
					notifyUser(R.string.qm_media_failed);
				} finally {
					if(!shared)
						discardShare(staged, null);
				}
			});
		} catch(RuntimeException | OutOfMemoryError error) {
			// 回投失败时释放 busy 和未交付的分享文件。
			busy = false;
			discardShare(staged, null);
		}
	}
}
