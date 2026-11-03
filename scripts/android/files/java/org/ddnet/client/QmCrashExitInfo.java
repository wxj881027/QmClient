package org.ddnet.client;

import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;
import android.util.Log;

import androidx.annotation.RequiresApi;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Locale;

// 系统记录的进程退出原因（API 30+）。
//
// 自己的信号处理器只能覆盖本进程主动捕获的致命信号；ART OOM、ANR、被系统
// 直接杀死这类退出走不到处理器里，但系统都留了记录。这里把最近一条转成与
// 己方报告同名的文本，由下次启动的原生搬运逻辑一并导出到「下载/QmClient」。
final class QmCrashExitInfo {
	private static final String TAG = "QmCrashExitInfo";
	private static final String SEEN_FILE = "qm_exit_info_seen.txt";
	private static final String REPORT_SUFFIX = "_exit_report.txt";

	// ApplicationExitInfo.REASON_* 的取值，硬编码避免 API 30 以下的类引用。
	private static final int REASON_CRASH = 4;
	private static final int REASON_CRASH_NATIVE = 5;
	private static final int REASON_ANR = 6;
	private static final int REASON_INITIALIZATION_FAILURE = 8;

	private QmCrashExitInfo() {
	}

	// 在 Activity.onCreate 里同步调用：读取很快，且必须在原生搬运逻辑扫描
	// 报告目录之前完成，否则本次记录要等到下一次启动才会被导出。
	static void collect(Context context) {
		if(Build.VERSION.SDK_INT < Build.VERSION_CODES.R)
			return;
		try {
			collectFromSystem(context);
		} catch(Throwable error) {
			// 退出原因只影响报告完整度，任何异常都不能影响启动。
			Log.w(TAG, "collect failed", error);
		}
	}

	@RequiresApi(Build.VERSION_CODES.R)
	private static void collectFromSystem(Context context) throws IOException {
		final ActivityManager manager = (ActivityManager)context.getSystemService(Context.ACTIVITY_SERVICE);
		if(manager == null)
			return;
		// API 30 只有带参数的重载；API 31 才加了无参的 getHistoricalProcessExitInfos()。
		final List<ApplicationExitInfo> infos = manager.getHistoricalProcessExitReasons(context.getPackageName(), 0, 5);
		if(infos == null)
			return;

		final File directory = new File(context.getExternalFilesDir(null), "dumps/QmClient_Crash");
		final long seen = readSeen(directory);
		for(ApplicationExitInfo info : infos) {
			final long timestamp = info.getTimestamp();
			if(timestamp <= seen)
				continue;
			final int reason = info.getReason();
			if(reason != REASON_CRASH && reason != REASON_CRASH_NATIVE && reason != REASON_ANR && reason != REASON_INITIALIZATION_FAILURE)
				continue;
			// 每次启动只取最近一条，避免把历史退出记录成批刷进下载目录。
			writeReport(directory, info, reason, timestamp);
			writeSeen(directory, timestamp);
			return;
		}
	}

	@RequiresApi(Build.VERSION_CODES.R)
	private static void writeReport(File directory, ApplicationExitInfo info, int reason, long timestamp) throws IOException {
		if(!directory.exists() && !directory.mkdirs())
			return;

		final String stamp = new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss", Locale.ROOT).format(new Date(timestamp));
		final File report = new File(directory, "QmClient_android_" + stamp + REPORT_SUFFIX);
		try(FileOutputStream out = new FileOutputStream(report)) {
			final StringBuilder header = new StringBuilder();
			header.append("QmClient system exit report\n");
			header.append("===========================\n");
			header.append("Platform: android\n");
			header.append("Recorded at: ").append(stamp).append('\n');
			header.append("Reason: ").append(reasonName(reason)).append(" (").append(reason).append(")\n");
			header.append("Description: ").append(safe(info.getDescription())).append('\n');
			header.append("Importance: ").append(info.getImportance()).append('\n');
			header.append("RSS: ").append(info.getRss()).append(" kB\n");
			header.append("PSS: ").append(info.getPss()).append(" kB\n");
			header.append("\nThis file was produced by the system, not by QmClient's own signal handler.\n\n");
			out.write(header.toString().getBytes(StandardCharsets.UTF_8));

			try(InputStream trace = info.getTraceInputStream()) {
				if(trace != null)
					copy(trace, out);
			}
		}

		// 与原生处理器一致的空标记，启动时据此判断哪些报告还没导出过。
		final File pending = new File(directory, report.getName() + ".pending");
		if(!pending.exists() && !pending.createNewFile())
			Log.w(TAG, "failed to create pending marker for " + report.getName());
	}

	private static String reasonName(int reason) {
		switch(reason) {
		case REASON_CRASH: return "CRASH";
		case REASON_CRASH_NATIVE: return "CRASH_NATIVE";
		case REASON_ANR: return "ANR";
		case REASON_INITIALIZATION_FAILURE: return "INITIALIZATION_FAILURE";
		default: return "OTHER";
		}
	}

	private static String safe(String text) {
		return text == null ? "(none)" : text;
	}

	private static void copy(InputStream input, OutputStream output) throws IOException {
		final byte[] buffer = new byte[16 * 1024];
		int read;
		while((read = input.read(buffer)) > 0)
			output.write(buffer, 0, read);
	}

	private static long readSeen(File directory) {
		final File state = new File(directory, SEEN_FILE);
		if(!state.isFile())
			return 0;
		try(InputStream input = new java.io.FileInputStream(state)) {
			final byte[] buffer = new byte[32];
			final int read = input.read(buffer);
			if(read <= 0)
				return 0;
			return Long.parseLong(new String(buffer, 0, read, StandardCharsets.UTF_8).trim());
		} catch(IOException | NumberFormatException error) {
			return 0;
		}
	}

	private static void writeSeen(File directory, long timestamp) {
		try(FileOutputStream out = new FileOutputStream(new File(directory, SEEN_FILE))) {
			out.write(Long.toString(timestamp).getBytes(StandardCharsets.UTF_8));
		} catch(IOException error) {
			Log.w(TAG, "failed to persist seen timestamp", error);
		}
	}
}
