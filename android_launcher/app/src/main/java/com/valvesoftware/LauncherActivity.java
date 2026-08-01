package com.valvesoftware;

import android.Manifest;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.View;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.Spinner;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

import java.io.File;
import java.util.ArrayList;
import java.util.List;

public class LauncherActivity extends AppCompatActivity {

    private static final int REQUEST_STORAGE_PERMISSION = 1001;
    private static final int REQUEST_MANAGE_STORAGE = 1002;

    private Spinner gameSpinner;
    private Spinner rendererSpinner;
    private Button launchButton;
    private Button settingsButton;
    private Button consoleButton;
    private TextView statusText;
    private SharedPreferences prefs;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_launcher);

        prefs = getSharedPreferences("launcher_prefs", MODE_PRIVATE);

        gameSpinner = findViewById(R.id.game_spinner);
        rendererSpinner = findViewById(R.id.renderer_spinner);
        launchButton = findViewById(R.id.launch_button);
        settingsButton = findViewById(R.id.settings_button);
        consoleButton = findViewById(R.id.console_button);
        statusText = findViewById(R.id.status_text);

        setupGameSpinner();
        setupRendererSpinner();

        launchButton.setOnClickListener(v -> checkPermissionsAndLaunch());
        settingsButton.setOnClickListener(v -> openSettings());
        consoleButton.setOnClickListener(v -> openConsole());

        updateStatus();
    }

    private void setupGameSpinner() {
        List<String> games = new ArrayList<>();
        games.add("Half-Life 2");
        games.add("Half-Life 2: Deathmatch");
        games.add("Half-Life 2: Episode One");
        games.add("Half-Life 2: Episode Two");
        games.add("Portal");

        ArrayAdapter<String> adapter = new ArrayAdapter<>(
                this,
                android.R.layout.simple_spinner_item,
                games
        );
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        gameSpinner.setAdapter(adapter);

        int savedGame = prefs.getInt("selected_game", 0);
        gameSpinner.setSelection(Math.min(savedGame, games.size() - 1));
    }

    private void setupRendererSpinner() {
        List<String> renderers = new ArrayList<>();
        renderers.add("OpenGL ES 2.0 (推荐)");
        renderers.add("OpenGL ES 3.0");
        renderers.add("Vulkan (实验性)");

        ArrayAdapter<String> adapter = new ArrayAdapter<>(
                this,
                android.R.layout.simple_spinner_item,
                renderers
        );
        adapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        rendererSpinner.setAdapter(adapter);

        int savedRenderer = prefs.getInt("selected_renderer", 0);
        rendererSpinner.setSelection(Math.min(savedRenderer, renderers.size() - 1));
    }

    private void updateStatus() {
        File nativeDir = new File(getApplicationInfo().nativeLibraryDir);
        File[] libs = nativeDir.listFiles((dir, name) -> name.endsWith(".so"));

        StringBuilder sb = new StringBuilder();
        sb.append("游戏目录: ").append(getAppDataPath()).append("\n\n");

        if (libs != null && libs.length > 0) {
            sb.append("已加载 ").append(libs.length).append(" 个原生库\n");
        } else {
            sb.append("警告: 未找到原生库 (.so 文件)\n");
        }

        String gameName = getGameDir();
        File gameDir = new File(getAppDataPath(), gameName);
        if (gameDir.exists() && gameDir.isDirectory()) {
            sb.append("游戏内容: 已安装\n");
        } else {
            sb.append("游戏内容: 未安装 (请将游戏文件放入上述目录)\n");
        }

        statusText.setText(sb.toString());
    }

    private String getGameDir() {
        int pos = gameSpinner.getSelectedItemPosition();
        switch (pos) {
            case 0: return "hl2";
            case 1: return "hl2mp";
            case 2: return "episodic";
            case 3: return "ep2";
            case 4: return "portal";
            default: return "hl2";
        }
    }

    private String getAppDataPath() {
        return getExternalFilesDir(null).getAbsolutePath();
    }

    private void checkPermissionsAndLaunch() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                startActivityForResult(intent, REQUEST_MANAGE_STORAGE);
                Toast.makeText(this, "请授予存储权限以读取游戏文件", Toast.LENGTH_LONG).show();
                return;
            }
            launchGame();
        } else {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                    != PackageManager.PERMISSION_GRANTED) {
                ActivityCompat.requestPermissions(this,
                        new String[]{
                                Manifest.permission.WRITE_EXTERNAL_STORAGE,
                                Manifest.permission.READ_EXTERNAL_STORAGE,
                                Manifest.permission.RECORD_AUDIO
                        }, REQUEST_STORAGE_PERMISSION);
            } else {
                launchGame();
            }
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions,
                                           @NonNull int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == REQUEST_STORAGE_PERMISSION) {
            boolean allGranted = true;
            for (int result : grantResults) {
                if (result != PackageManager.PERMISSION_GRANTED) {
                    allGranted = false;
                    break;
                }
            }
            if (allGranted) {
                launchGame();
            } else {
                Toast.makeText(this, "需要存储权限才能运行游戏", Toast.LENGTH_LONG).show();
            }
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, @Nullable Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQUEST_MANAGE_STORAGE) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
                if (Environment.isExternalStorageManager()) {
                    launchGame();
                } else {
                    Toast.makeText(this, "需要存储管理权限", Toast.LENGTH_LONG).show();
                }
            }
        }
    }

    private void launchGame() {
        prefs.edit()
                .putInt("selected_game", gameSpinner.getSelectedItemPosition())
                .putInt("selected_renderer", rendererSpinner.getSelectedItemPosition())
                .apply();

        String gameDir = getGameDir();
        String appDataPath = getAppDataPath();

        StringBuilder extraArgs = new StringBuilder();
        extraArgs.append("-game ").append(gameDir).append(" ");

        int renderer = rendererSpinner.getSelectedItemPosition();
        if (renderer == 0) {
            extraArgs.append("-gles2 ");
        } else if (renderer == 1) {
            extraArgs.append("-gles3 ");
        } else if (renderer == 2) {
            extraArgs.append("-vulkan ");
        }

        if (prefs.getBoolean("pref_enable_console", true)) {
            extraArgs.append("-console ");
        }

        if (prefs.getBoolean("pref_enable_dev", false)) {
            extraArgs.append("-dev ");
        }

        if (prefs.getBoolean("pref_enable_high_priority", true)) {
            extraArgs.append("-high ");
        }

        String additionalArgs = prefs.getString("pref_additional_args", "").trim();
        if (!additionalArgs.isEmpty()) {
            extraArgs.append(additionalArgs).append(" ");
        }

        Intent intent = new Intent(this, ValveActivity2.class);
        intent.putExtra("APP_DATA_PATH", appDataPath);
        intent.putExtra("EXTRA_ARGS", extraArgs.toString().trim());
        intent.putExtra("GAME_DIR", gameDir);

        startActivity(intent);
    }

    private void openSettings() {
        Intent intent = new Intent(this, SettingsActivity.class);
        startActivity(intent);
    }

    private void openConsole() {
        prefs.edit().putBoolean("pref_enable_console", true).apply();
        Toast.makeText(this, "已启用控制台，启动后按 ~ 键呼出", Toast.LENGTH_SHORT).show();
    }

    @Override
    protected void onResume() {
        super.onResume();
        updateStatus();
    }
}
