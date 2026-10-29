package org.ddnet.client;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Arrays;

// 无 Android 设备依赖；传入工作树 tmp 下的隔离测试目录。
public final class QmMediaFilesTest {
	private static void check(boolean condition, String message) {
		if(!condition)
			throw new AssertionError(message);
	}

	private interface CheckedCase {
		void run() throws Exception;
	}

	private static void withInterruptRestored(CheckedCase test) throws Exception {
		boolean previous = Thread.interrupted();
		try {
			test.run();
		} finally {
			Thread.interrupted();
			if(previous)
				Thread.currentThread().interrupt();
		}
	}

	private static void expectCancelled(InputStream input, OutputStream output) throws Exception {
		try {
			QmMediaFiles.copy(input, output);
			throw new AssertionError("Cancelled copy must not report completion");
		} catch(InterruptedIOException expected) {
			check(Thread.currentThread().isInterrupted(), "Copy preserves interrupted status");
		}
	}

	private static void directoryBoundary(Path temp) throws Exception {
		File root = temp.resolve("recordings").toFile();
		Files.createDirectories(root.toPath());
		check(QmMediaFiles.contains(root, new File(root, "auto/录像.demo")), "Nested media must be accessible");
		check(!QmMediaFiles.contains(root, new File(root, "../settings.cfg")), "Parent traversal must be rejected");
		check(!QmMediaFiles.contains(root, temp.resolve("recordings-other/run.demo").toFile()), "Sibling prefix must not match");
	}

	private static void mediaTypes() {
		check(QmMediaFiles.matches(new File("run.DEMO"), false), "Uppercase demo extension");
		check(QmMediaFiles.matches(new File("录像.MP4"), true), "Unicode video filename");
		check(!QmMediaFiles.matches(new File("settings.cfg"), false), "Config must not be listed");
		check(!QmMediaFiles.matches(new File("run.demo.tmp"), false), "Temporary recording must not be listed");
		check(!QmMediaFiles.matches(new File("run.demo"), true), "Video list must exclude demos");
	}

	private static void largeCopyKeepsBytes() throws Exception {
		byte[] source = new byte[200000];
		for(int i = 0; i < source.length; ++i)
			source[i] = (byte)i;
		ByteArrayOutputStream output = new ByteArrayOutputStream();
		QmMediaFiles.copy(new ByteArrayInputStream(source), output);
		check(Arrays.equals(source, output.toByteArray()), "All copy chunks must be retained");
	}

	private static void writeFailurePropagates() throws Exception {
		OutputStream failing = new OutputStream() {
			@Override public void write(int value) throws IOException { throw new IOException("Disk full"); }
		};
		try {
			QmMediaFiles.copy(new ByteArrayInputStream(new byte[] {1}), failing);
			throw new AssertionError("Copy must report output failure");
		} catch(IOException expected) { }
	}

	private static void cancellationPropagates() throws Exception {
		withInterruptRestored(() -> {
			Thread.currentThread().interrupt();
			expectCancelled(new ByteArrayInputStream(new byte[] {1}), new ByteArrayOutputStream());
		});
	}

	private static void cancelledEmptyCopyDoesNotReadOrFlush() throws Exception {
		withInterruptRestored(() -> {
			InputStream input = new InputStream() {
				@Override public int read() { throw new AssertionError("No read after cancellation"); }
			};
			OutputStream output = new ByteArrayOutputStream() {
				@Override public void flush() { throw new AssertionError("No flush after cancellation"); }
			};
			Thread.currentThread().interrupt();
			expectCancelled(input, output);
		});
	}

	private static void eofThatObservesCancellationCannotComplete() throws Exception {
		withInterruptRestored(() -> {
			InputStream input = new InputStream() {
				@Override public int read() {
					Thread.currentThread().interrupt();
					return -1;
				}
			};
			OutputStream output = new ByteArrayOutputStream() {
				@Override public void flush() { throw new AssertionError("Cancelled EOF must not flush"); }
			};
			expectCancelled(input, output);
		});
	}

	private static void cancellationAfterWriteStopsBeforeNextRead() throws Exception {
		withInterruptRestored(() -> {
			ByteArrayInputStream input = new ByteArrayInputStream(new byte[] {1}) {
				int reads;
				@Override public synchronized int read(byte[] data, int offset, int count) {
					check(++reads == 1, "Cancellation stops before another source read");
					return super.read(data, offset, count);
				}
			};
			ByteArrayOutputStream output = new ByteArrayOutputStream() {
				@Override public synchronized void write(byte[] data, int offset, int count) {
					super.write(data, offset, count);
					Thread.currentThread().interrupt();
				}
			};
			expectCancelled(input, output);
			check(Arrays.equals(output.toByteArray(), new byte[] {1}), "Keep already written bytes");
		});
	}

	private static void cancellationDuringFlushIsNotReportedAsSuccess() throws Exception {
		withInterruptRestored(() -> {
			ByteArrayOutputStream output = new ByteArrayOutputStream() {
				@Override public void flush() { Thread.currentThread().interrupt(); }
			};
			expectCancelled(new ByteArrayInputStream(new byte[] {1}), output);
			check(output.size() == 1, "Flush reached after complete data copy");
		});
	}

	private static void inputFailureIsNotConvertedToCompletion() throws Exception {
		IOException error = new IOException("Source read failed");
		InputStream input = new InputStream() {
			@Override public int read() throws IOException { throw error; }
		};
		try {
			QmMediaFiles.copy(input, new ByteArrayOutputStream());
			throw new AssertionError("Read failure must propagate");
		} catch(IOException caught) {
			check(caught == error, "Preserve original I/O error");
		}
	}

	private static void flushFailureIsNotConvertedToCompletion() throws Exception {
		IOException error = new IOException("Flush failed");
		OutputStream output = new ByteArrayOutputStream() {
			@Override public void flush() throws IOException { throw error; }
		};
		try {
			QmMediaFiles.copy(new ByteArrayInputStream(new byte[] {1}), output);
			throw new AssertionError("Flush failure must propagate");
		} catch(IOException caught) {
			check(caught == error, "Preserve flush failure");
		}
	}

	private static void borrowedStreamsAreNotClosedByCopy() throws Exception {
		InputStream input = new ByteArrayInputStream(new byte[] {1}) {
			@Override public void close() { throw new AssertionError("Caller owns input"); }
		};
		OutputStream output = new ByteArrayOutputStream() {
			@Override public void close() { throw new AssertionError("Caller owns output"); }
		};
		QmMediaFiles.copy(input, output);
	}

	public static void main(String[] arguments) throws Exception {
		if(arguments.length != 1)
			throw new IllegalArgumentException("Pass a workspace tmp directory");
		directoryBoundary(Path.of(arguments[0]));
		mediaTypes();
		largeCopyKeepsBytes();
		writeFailurePropagates();
		cancellationPropagates();
		cancelledEmptyCopyDoesNotReadOrFlush();
		eofThatObservesCancellationCannotComplete();
		cancellationAfterWriteStopsBeforeNextRead();
		cancellationDuringFlushIsNotReportedAsSuccess();
		inputFailureIsNotConvertedToCompletion();
		flushFailureIsNotConvertedToCompletion();
		borrowedStreamsAreNotClosedByCopy();
		System.out.println("QmMediaFiles: 12 scenarios passed");
	}
}
