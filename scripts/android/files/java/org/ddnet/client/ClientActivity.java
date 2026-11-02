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

	private final Object serverServiceMonitor = new Object();
	private Messenger serverServiceMessenger = null;
	private final ServiceConnection serverServiceConnection = new ServiceConnection() {
		@Override
		public void onServiceConnected(ComponentName name, IBinder service) {
			synchronized(serverServiceMonitor) {
				serverServiceMessenger = new Messenger(service);
			}
		}

		@Override
		public void onServiceDisconnected(ComponentName name) {
			synchronized(serverServiceMonitor) {
				serverServiceMessenger = null;
			}
		}
	};

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
		if(mediaExporter != null)
			mediaExporter.close();
		super.onDestroy();
		synchronized(serverServiceMonitor) {
			if(serverServiceMessenger != null) {
				unbindService(serverServiceConnection);
			}
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

	public void restartApp() {
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
		synchronized(serverServiceMonitor) {
			if(serverServiceMessenger != null) {
				return;
			}
			Intent startIntent = ServerService.createStartIntent(this, arguments);
			ContextCompat.startForegroundService(this, startIntent);
			bindService(startIntent, serverServiceConnection, 0);
		}
	}

	// Called from native code, see android_main.cpp
	public void executeCommand(String command) {
		synchronized(serverServiceMonitor) {
			if(serverServiceMessenger == null) {
				return;
			}
			try {
				serverServiceMessenger.send(ServerService.createExecuteCommandMessage(command));
			} catch (RemoteException e) {
				// Connection broken
				unbindService(serverServiceConnection);
			}
		}
	}

	// Called from native code, see android_main.cpp
	public boolean isServerRunning() {
		synchronized(serverServiceMonitor) {
			return serverServiceMessenger != null;
		}
	}
}
