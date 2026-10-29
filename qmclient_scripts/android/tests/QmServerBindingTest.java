package org.ddnet.client;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicReference;

// 普通JVM行为测试；fake仅替换Android绑定和Messenger发送，不复制生产状态机。
public final class QmServerBindingTest {
	private static void check(boolean condition, String message) {
		if(!condition)
			throw new AssertionError(message);
	}

	private static final class FakeConnection implements QmServerBinding.Connection {
		int binds;
		int unbinds;
		boolean accepted = true;
		RuntimeException bindError;
		Runnable onBind;
		Runnable onUnbind;

		@Override
		public boolean bind() {
			++binds;
			if(onBind != null)
				onBind.run();
			if(bindError != null)
				throw bindError;
			return accepted;
		}

		@Override
		public void unbind() {
			++unbinds;
			if(onUnbind != null)
				onUnbind.run();
		}
	}

	private static void pendingStartIsDeduplicated() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		binding.start(() -> first);
		binding.start(() -> { throw new AssertionError("Pending start must not create another connection"); });
		check(first.binds == 1, "Only one bind request");
		check(!binding.isRunning(), "Pending binding must not be reported as connected");
		binding.close();
		check(first.unbinds == 1, "Pending binding still needs unbind");
	}

	private static void connectedStartIsDeduplicatedAndCanSend() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		binding.start(() -> first);
		binding.connected(first, "channel");
		binding.start(() -> { throw new AssertionError("Connected service must not be restarted"); });
		check(binding.isRunning(), "Connected state");
		check(binding.send(channel -> channel.equals("channel")), "Use current channel");
		check(first.unbinds == 0, "Successful send retains binding");
		binding.close();
	}

	private static void closeBeforeConnectRejectsLateCallbackAndStart() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		binding.start(() -> first);
		binding.close();
		binding.connected(first, "late");
		check(!binding.isRunning(), "Destroyed owner cannot be reconnected");
		try {
			binding.start(() -> { throw new AssertionError("No factory after close"); });
			throw new AssertionError("Start after close must fail");
		} catch(IllegalStateException expected) { }
		binding.close();
		check(first.unbinds == 1, "Repeated close must not unbind twice");
	}

	private static void rejectedBindReleasesTrackingAndCanRetry() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection rejected = new FakeConnection();
		rejected.accepted = false;
		try {
			binding.start(() -> rejected);
			throw new AssertionError("Rejected bind must not succeed");
		} catch(IllegalStateException expected) { }
		check(rejected.unbinds == 1, "False bind still releases tracking");
		FakeConnection retry = new FakeConnection();
		binding.start(() -> retry);
		binding.connected(retry, "retry");
		check(binding.isRunning(), "Rejected bind must not block retry");
		binding.close();
	}

	private static void bindExceptionReleasesAndPreservesException() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		RuntimeException error = new SecurityException("Denied");
		first.bindError = error;
		try {
			binding.start(() -> first);
			throw new AssertionError("Binding error must propagate to JNI boundary");
		} catch(RuntimeException caught) {
			check(caught == error, "Original binding exception");
		}
		check(first.unbinds == 1, "Failed binding is released");
		binding.close();
		check(first.unbinds == 1, "No stale binding after failure");
	}

	private static void disconnectedBindingCanReconnectAndIsReleasedOnClose() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		binding.start(() -> first);
		binding.connected(first, "first");
		binding.disconnected(first);
		check(!binding.isRunning(), "Disconnection clears messenger");
		check(first.unbinds == 0, "System may reconnect the same binding");
		binding.connected(first, "reconnected");
		check(binding.send(channel -> channel.equals("reconnected")), "Reconnected messenger replaces old channel");
		binding.disconnected(first);
		binding.close();
		check(first.unbinds == 1, "Disconnected binding still belongs to owner");
	}

	private static void explicitRetryReleasesDisconnectedBindingAndIgnoresOldCallbacks() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		FakeConnection retry = new FakeConnection();
		binding.start(() -> first);
		binding.connected(first, "old");
		binding.disconnected(first);
		binding.start(() -> retry);
		check(first.unbinds == 1, "Retry releases prior binding");
		binding.connected(first, "stale");
		check(!binding.isRunning(), "Stale connection cannot publish");
		binding.connected(retry, "new");
		binding.disconnected(first);
		binding.failed(first);
		check(binding.send(channel -> channel.equals("new")), "Old callbacks cannot clear current channel");
		check(retry.unbinds == 0, "Old failure cannot unbind new connection");
		binding.close();
	}

	private static void deadOrNullBindingCanRetry() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		binding.start(() -> first);
		binding.failed(first);
		check(first.unbinds == 1, "Dead binding must be released");
		FakeConnection second = new FakeConnection();
		binding.start(() -> second);
		binding.connected(second, null);
		check(second.unbinds == 1, "Null channel cannot remain bound");
		binding.close();
		check(second.unbinds == 1, "Null binding is not released twice");
	}

	private static void failedSendClearsChannelAndDoesNotUnbindTwice() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		binding.start(() -> first);
		binding.connected(first, "dead");
		check(!binding.send(channel -> false), "Failed send is reported");
		check(!binding.isRunning(), "Failed send clears messenger");
		check(!binding.send(channel -> { throw new AssertionError("Do not reuse failed messenger"); }), "No second send");
		binding.close();
		check(first.unbinds == 1, "Failure and destroy share one release");
	}

	private static void senderExceptionAlsoReleasesChannel() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		binding.start(() -> first);
		binding.connected(first, "channel");
		try {
			binding.send(channel -> { throw new IllegalStateException("Message preparation failed"); });
			throw new AssertionError("Preparation error must propagate");
		} catch(IllegalStateException expected) { }
		check(!binding.isRunning() && first.unbinds == 1, "Exceptional send clears and releases binding");
		binding.close();
	}

	private static void unbindCallbackCannotResurrectReleasedChannel() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		first.onUnbind = () -> binding.connected(first, "late");
		binding.start(() -> first);
		binding.failed(first);
		check(!binding.isRunning(), "Release clears identity before platform unbind");
		binding.close();
		check(first.unbinds == 1, "Reentrant callback does not cause repeated unbind");
	}

	private static void synchronousConnectionCallbackUsesRegisteredCandidate() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		first.onBind = () -> binding.connected(first, "immediate");
		binding.start(() -> first);
		check(binding.send(channel -> channel.equals("immediate")), "Candidate is registered before bind callback");
		binding.close();
	}

	private static void missingFactoryResultDoesNotBlockRetry() {
		QmServerBinding<String> binding = new QmServerBinding<>();
		binding.connected(null, "invalid");
		check(!binding.isRunning(), "Missing callback identity cannot publish");
		try {
			binding.start(() -> null);
			throw new AssertionError("Missing connection must fail");
		} catch(IllegalStateException expected) { }
		FakeConnection retry = new FakeConnection();
		binding.start(() -> retry);
		check(retry.binds == 1, "Retry after factory failure");
		binding.close();
	}

	private static void missingConnectionCallbackCanRetryAfterTimeout() {
		AtomicLong clock = new AtomicLong(100);
		QmServerBinding<String> binding = new QmServerBinding<>(clock::get);
		FakeConnection first = new FakeConnection();
		FakeConnection retry = new FakeConnection();
		binding.start(() -> first);
		clock.addAndGet(QmServerBinding.BIND_TIMEOUT_NANOS - 1);
		binding.start(() -> { throw new AssertionError("Do not replace before deadline"); });
		clock.incrementAndGet();
		binding.start(() -> retry);
		check(first.unbinds == 1 && retry.binds == 1, "Missing callback must not block all future starts");
		binding.connected(first, "late");
		check(!binding.isRunning(), "Timed-out callback cannot publish");
		binding.connected(retry, "current");
		check(binding.isRunning(), "Retry can connect");
		binding.close();
	}

	private static void connectedServiceIsNotExpiredByBindingTimeout() {
		AtomicLong clock = new AtomicLong(Long.MAX_VALUE - 10);
		QmServerBinding<String> binding = new QmServerBinding<>(clock::get);
		FakeConnection first = new FakeConnection();
		binding.start(() -> first);
		clock.addAndGet(QmServerBinding.BIND_TIMEOUT_NANOS);
		binding.connected(first, "connected");
		binding.start(() -> { throw new AssertionError("Connected service has no bind deadline"); });
		check(first.unbinds == 0 && binding.isRunning(), "Only unresolved binding can expire");
		binding.close();
	}

	private static void concurrentStartsShareOneBinding() throws Exception {
		QmServerBinding<String> binding = new QmServerBinding<>();
		FakeConnection first = new FakeConnection();
		CountDownLatch entered = new CountDownLatch(1);
		CountDownLatch release = new CountDownLatch(1);
		CountDownLatch competing = new CountDownLatch(1);
		AtomicReference<Throwable> failure = new AtomicReference<>();
		first.onBind = () -> {
			entered.countDown();
			try {
				check(release.await(2, TimeUnit.SECONDS), "Bounded bind release");
			} catch(InterruptedException error) {
				Thread.currentThread().interrupt();
				throw new AssertionError(error);
			}
		};
		Thread starter = new Thread(() -> {
			try { binding.start(() -> first); } catch(Throwable error) { failure.set(error); }
		});
		Thread second = new Thread(() -> {
			competing.countDown();
			try { binding.start(() -> { throw new AssertionError("No second connection"); }); } catch(Throwable error) { failure.set(error); }
		});
		starter.setDaemon(true);
		second.setDaemon(true);
		try {
			starter.start();
			check(entered.await(2, TimeUnit.SECONDS), "Start reached bind");
			second.start();
			check(competing.await(2, TimeUnit.SECONDS), "Competing start issued");
		} finally {
			release.countDown();
			starter.join(3000);
			second.join(3000);
		}
		check(!starter.isAlive() && !second.isAlive(), "Workers finish within timeout");
		check(failure.get() == null, "Concurrent starts must not fail: " + failure.get());
		check(first.binds == 1, "One pending binding across threads");
		binding.close();
	}

	public static void main(String[] arguments) throws Exception {
		pendingStartIsDeduplicated();
		connectedStartIsDeduplicatedAndCanSend();
		closeBeforeConnectRejectsLateCallbackAndStart();
		rejectedBindReleasesTrackingAndCanRetry();
		bindExceptionReleasesAndPreservesException();
		disconnectedBindingCanReconnectAndIsReleasedOnClose();
		explicitRetryReleasesDisconnectedBindingAndIgnoresOldCallbacks();
		deadOrNullBindingCanRetry();
		failedSendClearsChannelAndDoesNotUnbindTwice();
		senderExceptionAlsoReleasesChannel();
		unbindCallbackCannotResurrectReleasedChannel();
		synchronousConnectionCallbackUsesRegisteredCandidate();
		missingFactoryResultDoesNotBlockRetry();
		missingConnectionCallbackCanRetryAfterTimeout();
		connectedServiceIsNotExpiredByBindingTimeout();
		concurrentStartsShareOneBinding();
		System.out.println("QmServerBinding: 16 scenarios passed");
	}
}
