package org.ddnet.client;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.io.OutputStream;
import java.util.Locale;

// 与 Android UI 分离，目录边界和复制失败可通过普通 JVM 验证。
public final class QmMediaFiles {
	private QmMediaFiles() {}

	public static boolean contains(File root, File file) throws IOException {
		String base = root.getCanonicalPath();
		String path = file.getCanonicalPath();
		String prefix = base.endsWith(File.separator) ? base : base + File.separator;
		return path.equals(base) || path.startsWith(prefix);
	}

	public static boolean matches(File file, boolean videos) {
		String name = file.getName().toLowerCase(Locale.ROOT);
		return videos ? name.endsWith(".mp4") : name.endsWith(".demo");
	}

	public static void copy(InputStream input, OutputStream output) throws IOException {
		checkCancelled();
		byte[] buffer = new byte[64 * 1024];
		while(true) {
			checkCancelled();
			int count = input.read(buffer);
			checkCancelled();
			if(count == -1)
				break;
			output.write(buffer, 0, count);
		}
		checkCancelled();
		output.flush();
		checkCancelled();
	}

	private static void checkCancelled() throws InterruptedIOException {
		if(Thread.currentThread().isInterrupted())
			throw new InterruptedIOException();
	}
}
