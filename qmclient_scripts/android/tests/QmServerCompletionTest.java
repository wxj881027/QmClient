package org.ddnet.client;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

// 普通JVM生产策略单元；fake只代替消息队列、提示、退出和线程等待。
public final class QmServerCompletionTest {
	private static void check(boolean condition, String message) {
		if(!condition)
			throw new AssertionError(message);
	}

	private static final class Queue implements QmServerCompletion.Poster {
		Runnable action;
		int posts;
		boolean accepted = true;
		RuntimeException error;
		boolean outOfMemory;

		@Override
		public boolean post(Runnable completion) {
			++posts;
			action = completion;
			if(error != null)
				throw error;
			if(outOfMemory)
				throw new OutOfMemoryError("Posting failed");
			return accepted;
		}
	}

	private static final class Waiter implements QmServerCompletion.Worker {
		boolean live;
		boolean interrupted;
		long timeout;
		int waits;

		@Override
		public void await(long millis) throws InterruptedException {
			++waits;
			timeout = millis;
			if(interrupted)
				throw new InterruptedException("Interrupted wait");
		}

		@Override
		public boolean alive() { return live; }
	}

	private static void acceptedCompletionReportsThenTerminates() {
		QmServerCompletion completion = new QmServerCompletion();
		Queue queue = new Queue();
		StringBuilder order = new StringBuilder();
		completion.finish(queue, () -> order.append("report "), () -> order.append("terminate"));
		check(order.length() == 0, "Accepted report remains queued");
		queue.action.run();
		check(order.toString().equals("report terminate"), "Report precedes normal termination");
	}

	private static void rejectedPostTerminatesImmediatelyAndIgnoresLateCallback() {
		QmServerCompletion completion = new QmServerCompletion();
		Queue queue = new Queue();
		queue.accepted = false;
		int[] exits = {0};
		completion.finish(queue, () -> { throw new AssertionError("Rejected report must not run"); }, () -> ++exits[0]);
		check(exits[0] == 1, "Rejected post exits inline");
		queue.action.run();
		check(exits[0] == 1, "Late rejected callback cannot terminate twice");
	}

	private static void postExceptionFallsBackWithoutDuplicateLateReport() {
		QmServerCompletion completion = new QmServerCompletion();
		Queue queue = new Queue();
		queue.error = new IllegalStateException("Looper stopped");
		int[] exits = {0};
		completion.finish(queue, () -> { throw new AssertionError("Posting failed"); }, () -> ++exits[0]);
		queue.action.run();
		check(exits[0] == 1, "Exception fallback claims completion once");
	}

	private static void postingOomStillTerminates() {
		QmServerCompletion completion = new QmServerCompletion();
		Queue queue = new Queue();
		queue.outOfMemory = true;
		int[] exits = {0};
		completion.finish(queue, () -> { throw new AssertionError("No report on allocation failure"); }, () -> ++exits[0]);
		check(exits[0] == 1, "Posting allocation failure cannot skip termination");
	}

	private static void reportExceptionDoesNotSkipTermination() {
		QmServerCompletion completion = new QmServerCompletion();
		Queue queue = new Queue();
		int[] exits = {0};
		completion.finish(queue, () -> { throw new IllegalArgumentException("Toast failed"); }, () -> ++exits[0]);
		queue.action.run();
		check(exits[0] == 1, "Report failure still terminates");
	}

	private static void reportOomDoesNotSkipTermination() {
		QmServerCompletion completion = new QmServerCompletion();
		Queue queue = new Queue();
		int[] exits = {0};
		completion.finish(queue, () -> { throw new OutOfMemoryError("Toast allocation failed"); }, () -> ++exits[0]);
		queue.action.run();
		check(exits[0] == 1, "Report OOM still terminates");
	}

	private static void repeatedCallbackAndFinishDoNotRepeatReport() {
		QmServerCompletion completion = new QmServerCompletion();
		Queue queue = new Queue();
		int[] reports = {0};
		int[] exits = {0};
		completion.finish(queue, () -> ++reports[0], () -> ++exits[0]);
		completion.finish(queue, () -> { throw new AssertionError("No second report"); }, () -> ++exits[0]);
		check(queue.posts == 1, "Only one dispatch");
		queue.action.run();
		queue.action.run();
		check(reports[0] == 1 && exits[0] == 1, "One report and termination attempt");
	}

	private static void synchronousPosterAlsoCompletesOnce() {
		QmServerCompletion completion = new QmServerCompletion();
		int[] exits = {0};
		completion.finish(action -> { action.run(); return false; }, () -> {}, () -> ++exits[0]);
		check(exits[0] == 1, "Inline completion suppresses fallback repetition");
	}

	private static void nullWorkerDoesNotWaitOrTerminate() {
		QmServerCompletion.awaitStop(null, () -> { throw new AssertionError("No worker to force stop"); });
	}

	private static void finishedWorkerKeepsOriginalWaitBudgetWithoutForcedExit() {
		Waiter worker = new Waiter();
		QmServerCompletion.awaitStop(worker, () -> { throw new AssertionError("Finished worker needs no forced exit"); });
		check(worker.waits == 1 && worker.timeout == 2500, "Original bounded wait budget");
	}

	private static void liveWorkerAfterWaitIsTerminated() {
		Waiter worker = new Waiter();
		worker.live = true;
		int[] exits = {0};
		QmServerCompletion.awaitStop(worker, () -> ++exits[0]);
		check(exits[0] == 1, "Unresponsive worker triggers original forced exit");
	}

	private static void interruptedLiveWorkerRestoresInterruptAndTerminates() {
		boolean previous = Thread.interrupted();
		try {
			Waiter worker = new Waiter();
			worker.live = true;
			worker.interrupted = true;
			int[] exits = {0};
			QmServerCompletion.awaitStop(worker, () -> {
				check(Thread.currentThread().isInterrupted(), "Restore interrupt before termination");
				++exits[0];
			});
			check(exits[0] == 1, "Interruption must not abandon live worker");
		} finally {
			Thread.interrupted();
			if(previous)
				Thread.currentThread().interrupt();
		}
	}

	private static void interruptedFinishedWorkerRestoresInterruptWithoutForcedExit() {
		boolean previous = Thread.interrupted();
		try {
			Waiter worker = new Waiter();
			worker.interrupted = true;
			QmServerCompletion.awaitStop(worker, () -> { throw new AssertionError("Worker already finished"); });
			check(Thread.currentThread().isInterrupted(), "Interrupted status retained");
		} finally {
			Thread.interrupted();
			if(previous)
				Thread.currentThread().interrupt();
		}
	}

	private static void concurrentCallbacksClaimCompletionOnce() throws Exception {
		QmServerCompletion completion = new QmServerCompletion();
		Queue queue = new Queue();
		AtomicInteger reports = new AtomicInteger();
		AtomicInteger exits = new AtomicInteger();
		AtomicReference<Throwable> failure = new AtomicReference<>();
		CountDownLatch start = new CountDownLatch(1);
		completion.finish(queue, reports::incrementAndGet, exits::incrementAndGet);
		Runnable invoke = () -> {
			try {
				check(start.await(2, TimeUnit.SECONDS), "Bounded callback release");
				queue.action.run();
			} catch(Throwable error) {
				failure.set(error);
			}
		};
		Thread first = new Thread(invoke);
		Thread second = new Thread(invoke);
		first.setDaemon(true);
		second.setDaemon(true);
		try {
			first.start();
			second.start();
		} finally {
			start.countDown();
			first.join(3000);
			second.join(3000);
		}
		check(!first.isAlive() && !second.isAlive(), "Callbacks finish within timeout");
		check(failure.get() == null, "Callback failure: " + failure.get());
		check(reports.get() == 1 && exits.get() == 1, "Concurrent callbacks share one completion");
	}

	public static void main(String[] arguments) throws Exception {
		acceptedCompletionReportsThenTerminates();
		rejectedPostTerminatesImmediatelyAndIgnoresLateCallback();
		postExceptionFallsBackWithoutDuplicateLateReport();
		postingOomStillTerminates();
		reportExceptionDoesNotSkipTermination();
		reportOomDoesNotSkipTermination();
		repeatedCallbackAndFinishDoNotRepeatReport();
		synchronousPosterAlsoCompletesOnce();
		nullWorkerDoesNotWaitOrTerminate();
		finishedWorkerKeepsOriginalWaitBudgetWithoutForcedExit();
		liveWorkerAfterWaitIsTerminated();
		interruptedLiveWorkerRestoresInterruptAndTerminates();
		interruptedFinishedWorkerRestoresInterruptWithoutForcedExit();
		concurrentCallbacksClaimCompletionOnce();
		System.out.println("QmServerCompletion: 14 scenarios passed");
	}
}
