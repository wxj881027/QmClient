package org.ddnet.client;

// 平台动作由调用方提供；统一管理服务进程退出的派发、回退与有界等待。
public final class QmServerCompletion {
	public interface Poster {
		boolean post(Runnable completion);
	}

	public interface Worker {
		void await(long millis) throws InterruptedException;
		boolean alive();
	}

	private boolean dispatched;
	private boolean completed;

	private synchronized boolean claim() {
		if(completed)
			return false;
		completed = true;
		return true;
	}

	public void finish(Poster poster, Runnable report, Runnable terminate) {
		synchronized(this) {
			if(dispatched)
				return;
			dispatched = true;
		}
		try {
			if(poster.post(() -> {
				if(!claim())
					return;
				try {
					report.run();
				} catch(RuntimeException | OutOfMemoryError ignored) {
					// 提示失败不能跳过原定的进程退出。
				} finally {
					terminate.run();
				}
			}))
				return;
		} catch(RuntimeException | OutOfMemoryError ignored) {
			// 构造回调/Handler或投递失败时在当前线程退出。
		}
		if(claim())
			terminate.run();
	}

	public static void awaitStop(Worker worker, Runnable terminate) {
		if(worker == null)
			return;
		try {
			worker.await(2500);
		} catch(InterruptedException interrupted) {
			Thread.currentThread().interrupt();
		} finally {
			if(worker.alive())
				terminate.run();
		}
	}
}
