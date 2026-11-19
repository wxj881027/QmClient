package org.ddnet.client;

// 普通JVM生产状态单元；fake只代替native接收结果或边界异常。
public final class QmServerCommandsTest {
	private static void check(boolean condition, String message) {
		if(!condition)
			throw new AssertionError(message);
	}

	private static final class Sender implements QmServerCommands.Sender {
		int calls;
		boolean accepted = true;
		RuntimeException error;
		boolean outOfMemory;
		String last;

		@Override
		public boolean send(String command) {
			++calls;
			last = command;
			if(error != null)
				throw error;
			if(outOfMemory)
				throw new OutOfMemoryError("Native call preparation failed");
			return accepted;
		}
	}

	private static void nullCommandIsRejectedWithoutNativeCall() {
		Sender sender = new Sender();
		QmServerCommands commands = new QmServerCommands(sender);
		check(!commands.execute(null), "Null command rejected");
		check(sender.calls == 0 && !commands.isStopping(), "No native side effect");
	}

	private static void emptyCommandStillUsesExistingNativePolicy() {
		Sender sender = new Sender();
		QmServerCommands commands = new QmServerCommands(sender);
		check(commands.execute(""), "Empty command can be accepted by native policy");
		check(sender.calls == 1 && sender.last.isEmpty(), "Forward unchanged");
		check(!commands.isStopping(), "Empty command does not stop service");
	}

	private static void ordinaryCommandKeepsUnicodeAndDoesNotStop() {
		Sender sender = new Sender();
		QmServerCommands commands = new QmServerCommands(sender);
		String text = "say 中文\ud83d\ude00";
		check(commands.execute(text), "Normal command accepted");
		check(sender.last.equals(text) && !commands.isStopping(), "Keep text and running state");
	}

	private static void rejectedShutdownCanRetryAndOnlyThenStops() {
		Sender sender = new Sender();
		sender.accepted = false;
		QmServerCommands commands = new QmServerCommands(sender);
		check(!commands.execute("shutdown"), "Rejection is reported");
		check(!commands.isStopping(), "Rejected shutdown cannot block retry");
		sender.accepted = true;
		check(commands.execute("shutdown"), "Later close path can submit again");
		check(commands.isStopping() && sender.calls == 2, "State commits after successful admission");
	}

	private static void nativeRuntimeExceptionDoesNotCommitShutdown() {
		Sender sender = new Sender();
		sender.error = new IllegalStateException("JNI call failed");
		QmServerCommands commands = new QmServerCommands(sender);
		check(!commands.execute("shutdown") && !commands.isStopping(), "Native exception remains retryable");
		sender.error = null;
		check(commands.execute("shutdown") && commands.isStopping(), "Retry can stop service");
	}

	private static void nativeOomDoesNotCommitShutdown() {
		Sender sender = new Sender();
		sender.outOfMemory = true;
		QmServerCommands commands = new QmServerCommands(sender);
		check(!commands.execute("shutdown") && !commands.isStopping(), "Preparation OOM remains retryable");
		sender.outOfMemory = false;
		check(commands.execute("shutdown") && commands.isStopping(), "Retry after allocation failure");
	}

	private static void acceptedShutdownIsDeduplicatedCaseInsensitively() {
		Sender sender = new Sender();
		QmServerCommands commands = new QmServerCommands(sender);
		check(commands.execute("SHUTDOWN"), "Original case-insensitive shutdown recognition");
		check(commands.execute("shutdown"), "Already accepted shutdown is idempotent");
		check(sender.calls == 1 && commands.isStopping(), "No duplicate native command");
	}

	private static void laterNormalFailureDoesNotUndoAcceptedShutdown() {
		Sender sender = new Sender();
		QmServerCommands commands = new QmServerCommands(sender);
		check(commands.execute("shutdown"), "Shutdown accepted");
		sender.accepted = false;
		check(!commands.execute("status"), "Unrelated command rejected");
		check(commands.isStopping(), "Prior accepted shutdown remains committed");
	}

	private static void shutdownWithArgumentsRemainsAnOrdinaryCommand() {
		Sender sender = new Sender();
		QmServerCommands commands = new QmServerCommands(sender);
		check(commands.execute("shutdown reason"), "Forward existing command syntax unchanged");
		check(!commands.isStopping(), "Do not change Java's original exact recognition");
	}

	public static void main(String[] arguments) {
		nullCommandIsRejectedWithoutNativeCall();
		emptyCommandStillUsesExistingNativePolicy();
		ordinaryCommandKeepsUnicodeAndDoesNotStop();
		rejectedShutdownCanRetryAndOnlyThenStops();
		nativeRuntimeExceptionDoesNotCommitShutdown();
		nativeOomDoesNotCommitShutdown();
		acceptedShutdownIsDeduplicatedCaseInsensitively();
		laterNormalFailureDoesNotUndoAcceptedShutdown();
		shutdownWithArgumentsRemainsAnOrdinaryCommand();
		System.out.println("QmServerCommands: 9 scenarios passed");
	}
}
