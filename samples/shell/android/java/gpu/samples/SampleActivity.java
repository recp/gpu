package gpu.samples;

import android.app.NativeActivity;
import android.app.Dialog;
import android.content.DialogInterface;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.view.Window;
import android.widget.Button;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;

public final class SampleActivity extends NativeActivity {
  private static final int BACKGROUND         = Color.rgb(7, 9, 13);
  private static final int PANEL              = Color.rgb(14, 18, 26);
  private static final int LINE               = Color.rgb(47, 52, 61);
  private static final int PRIMARY            = Color.rgb(243, 241, 235);
  private static final int SECONDARY          = Color.rgb(145, 143, 138);
  private static final int ACCENT             = Color.rgb(255, 112, 20);
  private static final int MAX_DOWNLOAD_BYTES = 64 * 1024 * 1024;

  private String       sampleId;
  private Dialog       codeDialog;
  private Dialog       statusDialog;
  private LinearLayout statusPanel;
  private ProgressBar  statusSpinner;
  private TextView     statusText;

  private static native void nativeFetchComplete(long request,
                                                 byte[] data,
                                                 String error);

  @Override
  protected void onCreate(Bundle state) {
    super.onCreate(state);
    sampleId = getIntent().getStringExtra("sample");
    getWindow().getDecorView().setBackgroundColor(BACKGROUND);
    addStatusOverlay();
    addCodeButton();
  }

  @Override
  protected void onDestroy() {
    if (codeDialog != null && codeDialog.isShowing()) {
      codeDialog.dismiss();
    }
    if (statusDialog != null && statusDialog.isShowing()) {
      statusDialog.dismiss();
    }
    super.onDestroy();
    if (isFinishing()) {
      android.os.Process.killProcess(android.os.Process.myPid());
    }
  }

  private void addCodeButton() {
    Button button = new Button(this);

    codeDialog = new Dialog(this);
    button.setText("Code");
    button.setTextColor(PRIMARY);
    button.setTextSize(12);
    button.setAllCaps(false);
    button.setTypeface(Typeface.MONOSPACE, Typeface.BOLD);
    button.setBackground(panel(10, PANEL, LINE));
    button.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View view) {
        showSources();
      }
    });

    codeDialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
    codeDialog.setCancelable(false);
    codeDialog.setContentView(button);
    codeDialog.show();

    Window window = codeDialog.getWindow();
    if (window != null) {
      android.view.WindowManager.LayoutParams params = window.getAttributes();

      window.setBackgroundDrawableResource(android.R.color.transparent);
      window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND);
      window.addFlags(
        android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE |
        android.view.WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL
      );
      window.setGravity(Gravity.TOP | Gravity.END);
      window.setLayout(dp(76), dp(42));
      params.x = dp(14);
      params.y = dp(14);
      window.setAttributes(params);
    }
  }

  private void addStatusOverlay() {
    statusDialog  = new Dialog(this);
    statusPanel   = new LinearLayout(this);
    statusSpinner = new ProgressBar(this);
    statusText    = new TextView(this);

    statusPanel.setOrientation(LinearLayout.HORIZONTAL);
    statusPanel.setGravity(Gravity.CENTER_VERTICAL);
    statusPanel.setPadding(dp(18), dp(14), dp(20), dp(14));
    statusPanel.setBackground(panel(12, PANEL, LINE));

    statusText.setText("Starting GPU sample");
    statusText.setTextColor(PRIMARY);
    statusText.setTextSize(13);
    statusText.setTypeface(Typeface.MONOSPACE);
    statusPanel.addView(statusSpinner, new LinearLayout.LayoutParams(
      dp(28),
      dp(28)
    ));
    LinearLayout.LayoutParams textParams = new LinearLayout.LayoutParams(
      ViewGroup.LayoutParams.WRAP_CONTENT,
      ViewGroup.LayoutParams.WRAP_CONTENT
    );

    textParams.setMargins(dp(14), 0, 0, 0);
    statusPanel.addView(statusText, textParams);

    statusDialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
    statusDialog.setCancelable(false);
    statusDialog.setContentView(statusPanel);
    statusDialog.show();

    Window window = statusDialog.getWindow();
    if (window != null) {
      window.setBackgroundDrawableResource(android.R.color.transparent);
      window.clearFlags(android.view.WindowManager.LayoutParams.FLAG_DIM_BEHIND);
      window.addFlags(
        android.view.WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE
      );
    }
  }

  private void setSampleStatus(final String message, final boolean failed) {
    runOnUiThread(new Runnable() {
      @Override
      public void run() {
        if (statusPanel == null || statusSpinner == null ||
            statusText == null) {
          return;
        }

        if (!statusDialog.isShowing()) {
          statusDialog.show();
        }
        statusPanel.setVisibility(View.VISIBLE);
        statusSpinner.setVisibility(failed ? View.GONE : View.VISIBLE);
        statusText.setText(message == null ? "GPU sample status" : message);
        statusText.setTextColor(failed ? ACCENT : PRIMARY);
      }
    });
  }

  private void setSampleReady() {
    runOnUiThread(new Runnable() {
      @Override
      public void run() {
        if (statusDialog != null && statusDialog.isShowing()) {
          statusDialog.dismiss();
        }
      }
    });
  }

  private void setSampleNotice(final String message) {
    runOnUiThread(new Runnable() {
      @Override
      public void run() {
        if (statusPanel == null || statusSpinner == null ||
            statusText == null) {
          return;
        }
        if (!statusDialog.isShowing()) {
          statusDialog.show();
        }
        statusPanel.setVisibility(View.VISIBLE);
        statusSpinner.setVisibility(View.GONE);
        statusText.setText(message == null ? "GPU sample notice" : message);
        statusText.setTextColor(PRIMARY);
      }
    });
  }

  private void fetchUrl(final String source, final long request) {
    Thread fetch = new Thread(new Runnable() {
      @Override
      public void run() {
        HttpURLConnection connection = null;
        byte[] data = null;
        String error = null;

        try {
          connection = (HttpURLConnection)new URL(source).openConnection();
          connection.setConnectTimeout(15000);
          connection.setReadTimeout(30000);
          connection.setInstanceFollowRedirects(true);
          connection.setRequestMethod("GET");
          connection.connect();
          if (connection.getResponseCode() < 200 ||
              connection.getResponseCode() >= 300) {
            throw new IOException("HTTP " + connection.getResponseCode());
          }

          long contentLength = connection.getContentLengthLong();
          if (contentLength > MAX_DOWNLOAD_BYTES) {
            throw new IOException("Download exceeds 64 MiB");
          }
          int initialCapacity = contentLength > 0
                                  ? (int)contentLength
                                  : 32768;
          try (InputStream input = connection.getInputStream();
               ByteArrayOutputStream output =
                 new ByteArrayOutputStream(initialCapacity)) {
            byte[] buffer = new byte[32768];
            int count;
            int total = 0;

            while ((count = input.read(buffer)) != -1) {
              if (count > MAX_DOWNLOAD_BYTES - total) {
                throw new IOException("Download exceeds 64 MiB");
              }
              output.write(buffer, 0, count);
              total += count;
            }
            data = output.toByteArray();
          }
        } catch (IOException failure) {
          error = failure.getMessage();
          if (error == null || error.isEmpty()) {
            error = "Android download failed";
          }
        } finally {
          if (connection != null) {
            connection.disconnect();
          }
        }
        nativeFetchComplete(request, data, error);
      }
    }, "gpu-sample-fetch");

    fetch.start();
  }

  private void showSources() {
    Dialog       dialog = new Dialog(this);
    LinearLayout page   = new LinearLayout(this);
    LinearLayout tabs   = new LinearLayout(this);
    TextView     code   = new TextView(this);
    ScrollView   scroll = new ScrollView(this);

    dialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
    page.setOrientation(LinearLayout.VERTICAL);
    page.setPadding(dp(16), dp(18), dp(16), dp(16));
    page.setBackgroundColor(BACKGROUND);

    TextView title = new TextView(this);
    title.setText(sampleId == null ? "Sample source" : sampleId);
    title.setTextColor(PRIMARY);
    title.setTextSize(20);
    title.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
    page.addView(title, matchWrap());

    tabs.setOrientation(LinearLayout.HORIZONTAL);
    tabs.setGravity(Gravity.START);
    addTab(tabs, code, "GPU C", SourceKind.C);
    addTab(tabs, code, "USL", SourceKind.USL);
    addTab(tabs, code, "SPIR-V", SourceKind.SPIRV);
    page.addView(tabs, margins(0, 12, 0, 12));

    code.setTextColor(PRIMARY);
    code.setTextSize(12);
    code.setTypeface(Typeface.MONOSPACE);
    code.setTextIsSelectable(true);
    code.setPadding(dp(14), dp(14), dp(14), dp(20));
    code.setBackground(panel(10, PANEL, LINE));
    code.setText(readSource(SourceKind.C));
    scroll.addView(code, matchWrap());
    page.addView(scroll, new LinearLayout.LayoutParams(
      ViewGroup.LayoutParams.MATCH_PARENT,
      0,
      1.0f
    ));

    Button close = new Button(this);
    close.setText("Close");
    close.setTextColor(PRIMARY);
    close.setAllCaps(false);
    close.setBackground(panel(10, PANEL, LINE));
    close.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View view) {
        dialog.dismiss();
      }
    });
    page.addView(close, margins(0, 12, 0, 0));

    dialog.setContentView(page);
    Window window = dialog.getWindow();
    if (window != null) {
      window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                       ViewGroup.LayoutParams.MATCH_PARENT);
      window.setBackgroundDrawableResource(android.R.color.transparent);
    }
    dialog.setOnShowListener(new DialogInterface.OnShowListener() {
      @Override
      public void onShow(DialogInterface value) {
        Window shown = dialog.getWindow();
        if (shown != null) {
          shown.setLayout(ViewGroup.LayoutParams.MATCH_PARENT,
                          ViewGroup.LayoutParams.MATCH_PARENT);
        }
      }
    });
    dialog.show();
  }

  private void addTab(LinearLayout tabs,
                      TextView code,
                      String label,
                      SourceKind kind) {
    Button button = new Button(this);

    button.setText(label);
    button.setTextColor(kind == SourceKind.C ? ACCENT : SECONDARY);
    button.setTextSize(12);
    button.setAllCaps(false);
    button.setBackgroundColor(Color.TRANSPARENT);
    button.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View view) {
        for (int i = 0; i < tabs.getChildCount(); i++) {
          ((Button)tabs.getChildAt(i)).setTextColor(SECONDARY);
        }
        button.setTextColor(ACCENT);
        code.setText(readSource(kind));
      }
    });
    tabs.addView(button, new LinearLayout.LayoutParams(
      ViewGroup.LayoutParams.WRAP_CONTENT,
      dp(42)
    ));
  }

  private String readSource(SourceKind kind) {
    String path;

    if (sampleId == null) {
      return "Sample id unavailable.";
    }
    if (kind == SourceKind.SPIRV) {
      path = "targets";
    } else {
      path = "sources/" + sampleId;
    }

    try {
      String[] files = getAssets().list(path);
      StringBuilder text = new StringBuilder();

      if (files == null) {
        return "Source unavailable.";
      }
      Arrays.sort(files);
      for (String file : files) {
        if (!matches(kind, file)) {
          continue;
        }
        if (kind == SourceKind.SPIRV &&
            !file.startsWith(sampleId + "-")) {
          continue;
        }
        if (text.length() > 0) {
          text.append("\n\n");
        }
        text.append(file).append("\n\n");
        text.append(withLineNumbers(read(path + "/" + file)));
      }
      return text.length() == 0 ? "Source unavailable." : text.toString();
    } catch (IOException error) {
      return "Failed to read source.";
    }
  }

  private boolean matches(SourceKind kind, String file) {
    if (kind == SourceKind.C) {
      return file.endsWith(".c") || file.endsWith(".h");
    }
    if (kind == SourceKind.USL) {
      return file.endsWith(".usl");
    }
    return file.endsWith(".spvasm");
  }

  private String read(String path) throws IOException {
    try (InputStream input = getAssets().open(path);
         ByteArrayOutputStream output = new ByteArrayOutputStream()) {
      byte[] buffer = new byte[8192];
      int count;

      while ((count = input.read(buffer)) >= 0) {
        output.write(buffer, 0, count);
      }
      return output.toString(StandardCharsets.UTF_8.name());
    }
  }

  private String withLineNumbers(String source) {
    String[] lines = source.split("\n", -1);
    StringBuilder numbered = new StringBuilder(source.length() +
                                                lines.length * 7);

    for (int i = 0; i < lines.length; i++) {
      numbered.append(String.format("%4d  %s", i + 1, lines[i]));
      if (i + 1 < lines.length) {
        numbered.append('\n');
      }
    }
    return numbered.toString();
  }

  private GradientDrawable panel(int radius, int fill, int stroke) {
    GradientDrawable background = new GradientDrawable();

    background.setColor(fill);
    background.setCornerRadius(dp(radius));
    background.setStroke(dp(1), stroke);
    return background;
  }

  private LinearLayout.LayoutParams matchWrap() {
    return new LinearLayout.LayoutParams(
      ViewGroup.LayoutParams.MATCH_PARENT,
      ViewGroup.LayoutParams.WRAP_CONTENT
    );
  }

  private LinearLayout.LayoutParams margins(int left,
                                            int top,
                                            int right,
                                            int bottom) {
    LinearLayout.LayoutParams params = matchWrap();

    params.setMargins(dp(left), dp(top), dp(right), dp(bottom));
    return params;
  }

  private int dp(int value) {
    return Math.round(value * getResources().getDisplayMetrics().density);
  }

  private enum SourceKind {
    C,
    USL,
    SPIRV
  }
}
