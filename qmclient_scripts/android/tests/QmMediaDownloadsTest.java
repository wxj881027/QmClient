package org.ddnet.client;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

public final class QmMediaDownloadsTest {
	private static final String CONTENT = "QmClient export";

	private static void check(boolean condition, String message) {
		if(!condition)
			throw new AssertionError(message);
	}

	private static final class Download implements QmMediaExporter.DownloadTarget {
		private boolean failClose;
		private boolean acceptPublish = true;
		private boolean closed;
		private boolean publishCalled;
		private boolean discarded;
		private final ByteArrayOutputStream contents = new ByteArrayOutputStream();

		@Override
		public OutputStream open() {
			return new OutputStream() {
				@Override
				public void write(int value) {
					contents.write(value);
				}

				@Override
				public void close() throws IOException {
					closed = true;
					if(failClose)
						throw new IOException("close failed");
				}
			};
		}

		@Override
		public boolean publish() {
			check(closed, "Publication must follow successful close");
			publishCalled = true;
			return acceptPublish;
		}

		@Override
		public void discard() {
			discarded = true;
		}
	}

	private static void publishesOnlyTheCompleteCopy(File source) throws IOException {
		Download target = new Download();
		QmMediaExporter.copyDownload(source, target);
		check(target.publishCalled && !target.discarded, "Successful export must keep its destination");
		check(CONTENT.equals(target.contents.toString(StandardCharsets.UTF_8)), "Published bytes differ from the source");
	}

	private static void closeFailureDiscardsWithoutPublishing(File source) throws IOException {
		Download target = new Download();
		target.failClose = true;
		try {
			QmMediaExporter.copyDownload(source, target);
			throw new AssertionError("Close error must fail the export");
		} catch(IOException expected) {
			check("close failed".equals(expected.getMessage()), "Close failure must reach the caller");
		}
		check(target.discarded && !target.publishCalled, "Close failure must leave no pending copy");
	}

	private static void failedPublicationDiscardsThePendingCopy(File source) throws IOException {
		Download target = new Download();
		target.acceptPublish = false;
		try {
			QmMediaExporter.copyDownload(source, target);
			throw new AssertionError("A provider that updates zero rows must fail the export");
		} catch(IOException expected) { }
		check(target.publishCalled && target.discarded, "Publication failure must remove the pending copy");
	}

	private static void legacyCopyKeepsExistingFileAndReturnsNewName(Path root, File source) throws IOException {
		Path downloads = Files.createDirectory(root.resolve("legacy"));
		Path original = Files.writeString(downloads.resolve(source.getName()), "existing");
		String copiedName = QmMediaExporter.copyToLegacyDownloads(downloads.toFile(), source);
		check(!original.getFileName().toString().equals(copiedName), "Existing public files must not be overwritten");
		check("existing".equals(Files.readString(original)), "Existing public file was changed");
		check(CONTENT.equals(Files.readString(downloads.resolve(copiedName))), "Legacy copy bytes differ");
	}

	private static void legacyCopyRemovesItsReservationWhenTheSourceDisappears(Path root) throws IOException {
		Path downloads = Files.createDirectory(root.resolve("legacy-failure"));
		File missing = root.resolve("missing.txt").toFile();
		try {
			QmMediaExporter.copyToLegacyDownloads(downloads.toFile(), missing);
			throw new AssertionError("A missing source must fail the public export");
		} catch(IOException expected) { }
		check(!Files.exists(downloads.resolve(missing.getName())), "Failed export left a public file behind");
	}

	public static void main(String[] arguments) throws IOException {
		Path root = Files.createTempDirectory(Path.of(arguments[0]), "media-downloads-");
		File source = Files.writeString(root.resolve("report.txt"), CONTENT).toFile();
		publishesOnlyTheCompleteCopy(source);
		closeFailureDiscardsWithoutPublishing(source);
		failedPublicationDiscardsThePendingCopy(source);
		legacyCopyKeepsExistingFileAndReturnsNewName(root, source);
		legacyCopyRemovesItsReservationWhenTheSourceDisappears(root);
	}
}
