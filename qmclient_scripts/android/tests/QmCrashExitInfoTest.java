package org.ddnet.client;

import android.app.ApplicationExitInfo;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.Stream;

public final class QmCrashExitInfoTest {
	private static final long TIMESTAMP = 1_700_000_000_100L;

	private static void check(boolean condition, String message) {
		if(!condition)
			throw new AssertionError(message);
	}

	private static List<Path> reports(Path directory) throws IOException {
		try(Stream<Path> files = Files.list(directory)) {
			return files.filter(file -> file.getFileName().toString().endsWith("_exit_report.txt"))
				.collect(Collectors.toList());
		}
	}

	private static Path onlyReport(Path directory) throws IOException {
		List<Path> files = reports(directory);
		check(files.size() == 1, "Expected one complete report");
		return files.get(0);
	}

	private static Path marker(Path report, String suffix) {
		return report.resolveSibling(report.getFileName().toString() + suffix);
	}

	private static void recordCrash(Path directory, long timestamp, String body) throws IOException {
		check(QmCrashExitInfo.recordExit(directory.toFile(), ApplicationExitInfo.REASON_CRASH, timestamp,
			(out, stamp, reason) -> out.write(body.getBytes(StandardCharsets.UTF_8))), "Crash must produce a report");
	}

	private static void initializationFailureIsReported(Path directory) throws IOException {
		check(QmCrashExitInfo.recordExit(directory.toFile(), ApplicationExitInfo.REASON_INITIALIZATION_FAILURE,
			TIMESTAMP, (out, stamp, reason) -> out.write(reason.getBytes(StandardCharsets.UTF_8))),
			"Android initialization failures need a report");
		Path report = onlyReport(directory);
		check(Files.readString(report).equals("INITIALIZATION_FAILURE"), "Use the system exit reason in the report");
		check(Files.isRegularFile(marker(report, ".pending")), "The native exporter must see the report");
	}

	private static void permissionChangeIsIgnored(Path directory) throws IOException {
		check(!QmCrashExitInfo.recordExit(directory.toFile(), ApplicationExitInfo.REASON_PERMISSION_CHANGE,
			TIMESTAMP, (out, stamp, reason) -> { throw new AssertionError("Permission change must not write a crash report"); }),
			"Permission changes must not be treated as initialization failures");
		check(!Files.exists(directory), "A non-crash exit must not produce any report files");
	}

	private static void consecutiveCrashesInOneSecondKeepBothReports(Path directory) throws IOException {
		recordCrash(directory, TIMESTAMP, "first");
		Path first = onlyReport(directory);
		recordCrash(directory, TIMESTAMP + 1, "second");
		check(reports(directory).size() == 2, "Crash times with different milliseconds need separate reports");
		check(Files.readString(first).equals("first"), "The second crash must not overwrite the first report");
	}

	private static void interruptedReportDoesNotAdvanceSeenTime(Path directory) throws IOException {
		recordCrash(directory, TIMESTAMP, "first");
		Path seen = directory.resolve("qm_exit_info_seen.txt");
		IOException failure = new IOException("Trace read failed");
		try {
			QmCrashExitInfo.recordExit(directory.toFile(), ApplicationExitInfo.REASON_CRASH_NATIVE,
				TIMESTAMP + 1000, (out, stamp, reason) -> {
					out.write('a');
					throw failure;
				});
			throw new AssertionError("A partial report must not be reported as ready");
		} catch(IOException caught) {
			check(caught == failure, "Preserve the original read failure");
		}
		check(Files.readString(onlyReport(directory)).equals("first"), "Keep the earlier complete report only");
		check(Files.readString(seen).equals(Long.toString(TIMESTAMP)), "Keep the previous seen time so the write can be retried");
	}

	private static void leftoverTemporaryFileIsReplacedOnRetry(Path directory) throws IOException {
		Files.createDirectories(directory);
		Path temporary = directory.resolve("qm_exit_info.tmp");
		Files.writeString(temporary, "abandoned partial report");
		recordCrash(directory, TIMESTAMP, "new");
		check(Files.readString(onlyReport(directory)).equals("new"), "Do not publish bytes from a previous unfinished write");
		check(!Files.exists(temporary), "Do not accumulate abandoned temporary files");
	}

	private static void markerFailureCanRetryWithoutRewritingReport(Path directory) throws IOException {
		recordCrash(directory, TIMESTAMP, "complete");
		Path report = onlyReport(directory);
		Path pending = marker(report, ".pending");
		Path seen = directory.resolve("qm_exit_info_seen.txt");
		Files.delete(seen);
		Files.delete(pending);
		Files.createDirectory(pending);
		QmCrashExitInfo.ReportWriter noRewrite = (out, stamp, reason) -> { throw new AssertionError("Reuse the complete report on retry"); };
		try {
			QmCrashExitInfo.recordExit(directory.toFile(), ApplicationExitInfo.REASON_CRASH, TIMESTAMP, noRewrite);
			throw new AssertionError("A directory is not an export marker");
		} catch(IOException expected) {
			check(!Files.exists(seen), "Do not advance the seen time until a regular marker exists");
		}
		Files.delete(pending);
		check(QmCrashExitInfo.recordExit(directory.toFile(), ApplicationExitInfo.REASON_CRASH, TIMESTAMP, noRewrite),
			"Publishing the marker must be retryable");
		check(Files.isRegularFile(pending), "The retry must publish a regular export marker");
		check(Files.readString(report).equals("complete"), "The retry must preserve the report contents");
	}

	private static void previouslyExportedReportIsNotQueuedAgain(Path directory) throws IOException {
		recordCrash(directory, TIMESTAMP, "exported");
		Path report = onlyReport(directory);
		Path pending = marker(report, ".pending");
		Files.move(pending, marker(report, ".exported"));
		Path seen = directory.resolve("qm_exit_info_seen.txt");
		Files.delete(seen);
		check(QmCrashExitInfo.recordExit(directory.toFile(), ApplicationExitInfo.REASON_CRASH, TIMESTAMP,
			(out, stamp, reason) -> { throw new AssertionError("Keep the already exported report"); }), "Recover the missing seen time");
		check(!Files.exists(pending), "The existing export marker must suppress re-queueing");
		check(Files.readString(seen).equals(Long.toString(TIMESTAMP)), "Repair the seen time on retry");
	}

	public static void main(String[] arguments) throws IOException {
		if(arguments.length != 1)
			throw new IllegalArgumentException("Pass a workspace tmp directory");
		Path root = Files.createTempDirectory(Path.of(arguments[0]), "android-exit-info-");
		initializationFailureIsReported(root.resolve("initialization"));
		permissionChangeIsIgnored(root.resolve("permissions"));
		consecutiveCrashesInOneSecondKeepBothReports(root.resolve("consecutive"));
		interruptedReportDoesNotAdvanceSeenTime(root.resolve("partial"));
		leftoverTemporaryFileIsReplacedOnRetry(root.resolve("retry"));
		markerFailureCanRetryWithoutRewritingReport(root.resolve("marker"));
		previouslyExportedReportIsNotQueuedAgain(root.resolve("exported"));
	}
}
