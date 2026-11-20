package org.ddnet.client;

import java.util.ArrayList;
import java.util.List;

public final class QmServerActivityLifecycleTest {
	private static void check(boolean condition, String message) {
		if(!condition)
			throw new AssertionError(message);
	}

	private static void initializationInTheClientDoesNotLoadTheServerLibrary() throws ClassNotFoundException {
		// 在未安装 DDNet-Server 本地库的测试 JVM 里初始化 Java 服务类。
		Class.forName(ServerService.class.getName(), true, ServerService.class.getClassLoader());
	}

	private static void acceptedBindingKeepsTheStartedService() {
		List<String> events = new ArrayList<>();
		boolean bound = ClientActivity.startAndBindServer(
			() -> events.add("start"), () -> { events.add("bind"); return true; }, () -> events.add("stop"));
		check(bound && events.equals(List.of("start", "bind")), "Successful bind must keep the started service");
	}

	private static void rejectedBindingStopsTheStartedService() {
		List<String> events = new ArrayList<>();
		boolean bound = ClientActivity.startAndBindServer(
			() -> events.add("start"), () -> { events.add("bind"); return false; }, () -> events.add("stop"));
		check(!bound && events.equals(List.of("start", "bind", "stop")), "Rejected binding must release the started service");
	}

	private static void bindingExceptionStopsTheStartedService() {
		List<String> events = new ArrayList<>();
		RuntimeException bindingError = new IllegalStateException("Binding failed");
		try {
			ClientActivity.startAndBindServer(
				() -> events.add("start"), () -> { events.add("bind"); throw bindingError; }, () -> events.add("stop"));
			throw new AssertionError("Binding failure must be visible to the caller");
		} catch(RuntimeException error) {
			check(error == bindingError, "Binding error was replaced");
		}
		check(events.equals(List.of("start", "bind", "stop")), "Binding exception left a started service");
	}

	private static void startExceptionDoesNotBindOrStopAnUnstartedService() {
		List<String> events = new ArrayList<>();
		RuntimeException startError = new IllegalStateException("Start denied");
		try {
			ClientActivity.startAndBindServer(
				() -> { events.add("start"); throw startError; },
				() -> { events.add("bind"); return true; },
				() -> events.add("stop"));
			throw new AssertionError("Failed start must be visible to the caller");
		} catch(RuntimeException error) {
			check(error == startError, "Start error was replaced");
		}
		check(events.equals(List.of("start")), "Failed start must not proceed to binding or teardown");
	}

	public static void main(String[] arguments) throws ClassNotFoundException {
		initializationInTheClientDoesNotLoadTheServerLibrary();
		acceptedBindingKeepsTheStartedService();
		rejectedBindingStopsTheStartedService();
		bindingExceptionStopsTheStartedService();
		startExceptionDoesNotBindOrStopAnUnstartedService();
	}
}
