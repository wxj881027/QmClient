package org.ddnet.client;

import android.app.NativeActivity;
import android.content.*;
import android.content.pm.ActivityInfo;
import android.os.*;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.util.Log;
import android.widget.Toast;

import java.io.File;

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

		// 手机端默认 GLES：Vulkan 在部分驱动的表面/交换链生命周期上仍不稳定，
		// 先默认走 GLES；需要 Vulkan 时用 intent extra "gfx-backend=Vulkan" 覆盖。
		launchArguments = new String[] {"gfx_backend", "GLES"};
		Intent intent = getIntent();
		if(intent != null) {
			String gfxBackend = intent.getStringExtra("gfx-backend");
			if(gfxBackend != null) {
				if(gfxBackend.equals("Vulkan")) {
					launchArguments = new String[] {"gfx_backend", "Vulkan"};
				} else if(gfxBackend.equals("GLES")) {
					launchArguments = new String[] {"gfx_backend", "GLES"};
				}
			}
		}

		super.onCreate(savedInstanceState);
		// 先落系统退出记录：原生搬运逻辑随后就要扫描报告目录，晚于它就得等下次启动。
		QmCrashExitInfo.collect(this);
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
// 由 SDL 主线程通过 JNI 调用：把单个文件导出到公共「下载/QmClient」，
	// Android 10+ 走 MediaStore（不需要存储权限），旧版本走公共目录直写。
// 用系统文件管理器打开「下载」目录（DocumentsUI），不再自绘文件列表。
	public void openDownloadsFolder() {
		runOnUiThread(() -> {
			try {
				final Uri root = Uri.parse("content://com.android.externalstorage.documents/root/downloads");
				Intent intent = new Intent(Intent.ACTION_VIEW);
				intent.setDataAndType(root, "vnd.android.document/directory");
				if(Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
					final Uri target = Uri.parse("content://com.android.externalstorage.documents/document/primary%3ADownload%2FQmClient");
					intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI, target);
				}
				startActivity(intent);
			} catch(Throwable e) {
				Log.w("qmclient", "open downloads folder failed", e);
				Toast.makeText(this, "文件已保存到 下载/QmClient", Toast.LENGTH_LONG).show();
			}
		});
	}

	public void exportToDownloads(String path) {
		final File source = new File(path);
		if(!source.isFile()) {
			runOnUiThread(() -> Toast.makeText(this, "导出失败：文件不存在", Toast.LENGTH_LONG).show());
			return;
		}
		new Thread(() -> {
			String message;
			try {
				final String saved = QmMediaExporter.copyToDownloads(this, source);
				message = saved != null ? "已保存到 下载/QmClient/" + saved : "导出失败";
			} catch(Throwable e) {
				Log.w("qmclient", "export failed", e);
				message = "导出失败：" + e.getMessage();
			}
			final String text = message;
			runOnUiThread(() -> Toast.makeText(this, text, Toast.LENGTH_LONG).show());
		}).start();
	}

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
