package com.recp.gpu.samples;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.IOException;
import java.io.InputStream;

public final class GalleryActivity extends Activity {
  private static final int BACKGROUND = Color.rgb(7, 9, 13);
  private static final int PANEL      = Color.rgb(14, 18, 26);
  private static final int LINE       = Color.rgb(47, 52, 61);
  private static final int PRIMARY    = Color.rgb(243, 241, 235);
  private static final int SECONDARY  = Color.rgb(145, 143, 138);
  private static final int ACCENT     = Color.rgb(255, 112, 20);

  @Override
  protected void onCreate(Bundle state) {
    super.onCreate(state);
    getWindow().setStatusBarColor(BACKGROUND);
    getWindow().setNavigationBarColor(BACKGROUND);
    setTitle("GPU + USL Samples");
    setContentView(createContent());
  }

  private View createContent() {
    ScrollView scroll = new ScrollView(this);
    LinearLayout page = new LinearLayout(this);

    scroll.setBackgroundColor(BACKGROUND);
    scroll.setFillViewport(true);
    page.setOrientation(LinearLayout.VERTICAL);
    page.setPadding(dp(20), dp(28), dp(20), dp(36));

    TextView eyebrow = text("VULKAN / ANDROID", 12, ACCENT, true);
    TextView title   = text("GPU + USL Samples", 28, PRIMARY, true);
    TextView detail  = text("Native render and compute paths.", 15,
                            SECONDARY, false);
    page.addView(eyebrow);
    page.addView(title, margin(0, 8, 0, 4));
    page.addView(detail, margin(0, 0, 0, 24));
    page.addView(card("triangle",
                      "RENDER / 01",
                      "First pixel",
                      "Bufferless USL triangle.",
                      "previews/triangle.png"));
    page.addView(card("textured-cube",
                      "RENDER / 02",
                      "Rotating cube",
                      "Texture sampling, indexed draw, and depth.",
                      "previews/textured-cube.png"),
                 margin(0, 18, 0, 0));
    scroll.addView(page);
    return scroll;
  }

  private View card(String id,
                    String category,
                    String title,
                    String detail,
                    String preview) {
    LinearLayout card = new LinearLayout(this);
    ImageView image    = new ImageView(this);

    card.setOrientation(LinearLayout.VERTICAL);
    card.setPadding(dp(12), dp(12), dp(12), dp(16));
    card.setBackground(panel(18, PANEL, LINE));
    card.setClipToOutline(true);
    card.setClickable(true);
    card.setFocusable(true);
    card.setOnClickListener(new View.OnClickListener() {
      @Override
      public void onClick(View view) {
        Intent intent = new Intent(GalleryActivity.this,
                                   SampleActivity.class);
        intent.putExtra("sample", id);
        startActivity(intent);
      }
    });

    image.setImageBitmap(load(preview));
    image.setScaleType(ImageView.ScaleType.CENTER_CROP);
    image.setBackground(panel(12, Color.rgb(3, 8, 23), LINE));
    image.setClipToOutline(true);
    card.addView(image, new LinearLayout.LayoutParams(
      ViewGroup.LayoutParams.MATCH_PARENT,
      dp(206)
    ));
    card.addView(text(category, 11, ACCENT, true), margin(2, 14, 0, 0));
    card.addView(text(title, 22, PRIMARY, true), margin(2, 5, 0, 0));
    card.addView(text(detail, 14, SECONDARY, false), margin(2, 5, 0, 0));
    return card;
  }

  private TextView text(String value,
                        int size,
                        int color,
                        boolean bold) {
    TextView text = new TextView(this);

    text.setText(value);
    text.setTextColor(color);
    text.setTextSize(size);
    text.setGravity(Gravity.START);
    text.setTypeface(bold
                       ? Typeface.create("sans-serif", Typeface.BOLD)
                       : Typeface.create("sans-serif", Typeface.NORMAL));
    return text;
  }

  private GradientDrawable panel(int radius, int fill, int stroke) {
    GradientDrawable background = new GradientDrawable();

    background.setColor(fill);
    background.setCornerRadius(dp(radius));
    background.setStroke(dp(1), stroke);
    return background;
  }

  private LinearLayout.LayoutParams margin(int left,
                                           int top,
                                           int right,
                                           int bottom) {
    LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
      ViewGroup.LayoutParams.MATCH_PARENT,
      ViewGroup.LayoutParams.WRAP_CONTENT
    );

    params.setMargins(dp(left), dp(top), dp(right), dp(bottom));
    return params;
  }

  private Bitmap load(String path) {
    try (InputStream stream = getAssets().open(path)) {
      return BitmapFactory.decodeStream(stream);
    } catch (IOException error) {
      return null;
    }
  }

  private int dp(int value) {
    return Math.round(value * getResources().getDisplayMetrics().density);
  }
}
