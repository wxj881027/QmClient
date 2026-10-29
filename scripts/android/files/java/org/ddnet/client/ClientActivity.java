package org.ddnet.client;

import android.app.NativeActivity;
import android.content.*;
import android.content.pm.ActivityInfo;
import android.os.*;

import androidx.core.content.ContextCompat;

import org.libsdl.app.SDLActivity;

public class ClientActivity extends SDLActivity {

	private static final int COMMAND_RESTART_APP = SDLActivity.COMMAND_USER + 1;

	private String[] launchArguments = new String[0];
	private QmMediaExporter mediaExporter;

	private final QmServerBinding<Messenger> serverBinding = new QmServerBinding<>();

	private final class ServerConnection implements ServiceConnection, QmServerBinding.Connection {
		private final Intent intent;
		private boolean bindingAttempted;

		ServerConnection(String[] arguments) {
			intent = ServerService.createStartIntent(ClientActivity.this, arguments);
		}

		@Override
		public boolean bind() {
			ContextCompat.startForegroundService(ClientActivity.this, intent);
			bindingAttempted = true;
			return bindService(intent, this, 0);
		}

		@Override
		public void unbind() {
			if(!bindingAttempted)
				return;
			bindingAttempted = false;
			try {
				unbindService(this);
			} catch(IllegalArgumentException e) {
				// bind抛出异常且未登记连接时，框架可能已经没有可解绑记录。
			}
		}

		@Override
		public void onServiceConnected(ComponentName name, IBinder service) {
			serverBinding.connected(this, new Messenger(service));
		}

		@Override
		public void onServiceDisconnected(ComponentName name) {
			serverBinding.disconnected(this);
		}

		@Override
		public void onBindingDied(ComponentName name) {
			serverBinding.failed(this);
		}

		@Override
		public void onNullBinding(ComponentName name) {
			serverBinding.failed(this);
		}
	}

	@Override
	protected String[] getLibraries() {
		return new String[] {
			"DDNet",
		};
	}

	@Override
	public void onCreate(Bundle savedInstanceState) {
		setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE);

		Intent intent = getIntent();
		if(intent != null) {
			String gfxBackend = intent.getStringExtra("gfx-backend");
			if(gfxBackend != null) {
				if(gfxBackend.equals("Vulkan")) {
					launchArguments = new String[] {"gfx_backend Vulkan"};
				} else if(gfxBackend.equals("GLES")) {
					launchArguments = new String[] {"gfx_backend GLES"};
				}
			}
		}

		super.onCreate(savedInstanceState);
		mediaExporter = new QmMediaExporter(this, savedInstanceState);
	}

	@Override
	protected void onSaveInstanceState(Bundle state) {
		if(mediaExporter != null)
			mediaExporter.saveState(state);
		super.onSaveInstanceState(state);
	}

	@Override
	protected void onActivityResult(int requestCode, int resultCode, Intent data) {
		if(mediaExporter != null && mediaExporter.onActivityResult(requestCode, resultCode, data))
			return;
		super.onActivityResult(requestCode, resultCode, data);
	}

	// 由 SDL 主线程通过 JNI 调用，原生对话框在 Android UI 线程打开。
	public void showMediaFiles(String directory, boolean videos, String[] excluded) {
		runOnUiThread(() -> {
			if(mediaExporter != null)
				mediaExporter.browse(directory, videos, excluded);
		});
	}

	@Override
	protected void onDestroy() {
		try {
			try {
				serverBinding.close();
			} finally {
				stopServer();
			}
			if(mediaExporter != null)
				mediaExporter.close();
		} finally {
			super.onDestroy();
		}
	}

	@Override
	protected String[] getArguments() {
		return launchArguments;
	}

	@Override
	protected boolean onUnhandledMessage(int command, Object param) {
		switch(command) {
		case COMMAND_RESTART_APP:
			restartApp();
			return true;
		}
		return false;
	}

	private void restartApp() {
		Intent restartIntent =
			Intent.makeRestartActivityTask(
				getPackageManager().getLaunchIntentForPackage(
					getPackageName()
				).getComponent()
			);
		restartIntent.setPackage(getPackageName());
		startActivity(restartIntent);
	}

	// Called from native code, see android_main.cpp
	public void startServer(String[] arguments) {
		serverBinding.start(() -> new ServerConnection(arguments));
	}

	// Called from native code, see android_main.cpp
	public void stopServer() {
		try {
			executeCommand("shutdown");
		} finally {
			try {
				serverBinding.cancel();
			} finally {
				// 服务的started状态独立于绑定，即使未收到connected也要显式停止。
				stopService(new Intent(this, ServerService.class));
			}
		}
	}

	// Called from native code, see android_main.cpp
	public void executeCommand(String command) {
		serverBinding.send(messenger -> {
			try {
				messenger.send(ServerService.createExecuteCommandMessage(command));
				return true;
			} catch (RemoteException e) {
				return false;
			}
		});
	}

	// Called from native code, see android_main.cpp
	public boolean isServerRunning() {
		return serverBinding.isRunning();
	}
}
