package org.ddnet.client;

import java.util.concurrent.TimeUnit;
import java.util.function.LongSupplier;

// JNI线程与Android主线程共用此owner；平台回调携带每次绑定的身份。
public final class QmServerBinding<T> implements AutoCloseable {
	static final long BIND_TIMEOUT_NANOS = TimeUnit.SECONDS.toNanos(15);
	public interface Connection {
		boolean bind();
		void unbind();
	}

	public interface Factory {
		Connection create();
	}

	public interface Sender<T> {
		boolean send(T channel);
	}

	private Connection connection;
	private T channel;
	private final LongSupplier clock;
	private long bindStarted;
	private boolean pending;
	private boolean closed;

	public QmServerBinding() {
		this(System::nanoTime);
	}

	QmServerBinding(LongSupplier clock) {
		this.clock = clock;
	}

	public synchronized void start(Factory factory) {
		if(closed)
			throw new IllegalStateException("Activity is destroyed");
		// 服务创建时崩溃可能永远没有connected回调；后续主动启动允许超时重试。
		if(connection != null && (channel != null || (pending && clock.getAsLong() - bindStarted < BIND_TIMEOUT_NANOS)))
			return;
		release(connection);
		Connection candidate = factory.create();
		if(candidate == null)
			throw new IllegalStateException("Missing service connection");
		bindStarted = clock.getAsLong();
		connection = candidate;
		pending = true;
		boolean accepted = false;
		try {
			accepted = candidate.bind();
			if(!accepted)
				throw new IllegalStateException("Service binding failed");
		} finally {
			if(!accepted)
				release(candidate);
		}
	}

	public synchronized void connected(Connection source, T value) {
		if(source == null || source != connection || closed)
			return;
		if(value == null) {
			release(source);
			return;
		}
		channel = value;
		pending = false;
	}

	public synchronized void disconnected(Connection source) {
		if(source != connection)
			return;
		channel = null;
		pending = false;
		// 断开并未释放绑定，仍接受系统重连；再次主动启动时先释放旧绑定。
	}

	public synchronized void failed(Connection source) {
		release(source);
	}

	public synchronized boolean send(Sender<T> sender) {
		if(channel == null)
			return false;
		Connection target = connection;
		boolean sent = false;
		try {
			sent = sender.send(channel);
			return sent;
		} finally {
			if(!sent)
				release(target);
		}
	}

	public synchronized boolean isRunning() {
		return channel != null;
	}

	private void release(Connection source) {
		if(source == null || source != connection)
			return;
		connection = null;
		channel = null;
		pending = false;
		// 先清空状态，避免解绑期间的回调或重复清理再次使用同一连接。
		source.unbind();
	}

	@Override
	public synchronized void close() {
		closed = true;
		release(connection);
	}

	public synchronized void cancel() {
		release(connection);
	}
}
