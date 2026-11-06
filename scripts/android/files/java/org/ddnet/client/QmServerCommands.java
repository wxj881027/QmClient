package org.ddnet.client;

// Service主线程专用；只有native已接收停服命令才提交停止状态。
public final class QmServerCommands {
	public interface Sender {
		boolean send(String command);
	}

	private final Sender sender;
	private boolean stopping;

	public QmServerCommands(Sender sender) {
		this.sender = sender;
	}

	public boolean execute(String command) {
		if(command == null)
			return false;
		boolean shutdown = "shutdown".equalsIgnoreCase(command);
		if(shutdown && stopping)
			return true;
		try {
			if(!sender.send(command))
				return false;
		} catch(RuntimeException | OutOfMemoryError error) {
			return false;
		}
		if(shutdown)
			stopping = true;
		return true;
	}

	public boolean isStopping() {
		return stopping;
	}
}
