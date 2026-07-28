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
import android.widget.Toast;

import java.io.IOException;
import java.io.InputStream;

public final class GalleryActivity extends Activity {
  private static final int BACKGROUND = Color.rgb(7, 9, 13);
  private static final int PANEL      = Color.rgb(14, 18, 26);
  private static final int LINE       = Color.rgb(47, 52, 61);
  private static final int PRIMARY    = Color.rgb(243, 241, 235);
  private static final int SECONDARY  = Color.rgb(145, 143, 138);
  private static final int ACCENT     = Color.rgb(255, 112, 20);

  private static final Sample[] SAMPLES = {
    new Sample("triangle", "Render / 01", "First pixel",
               "A bufferless triangle through the portable render path.",
               "triangle.png", true),
    new Sample("textured-quad", "Transfer + Binding / 02", "Transfer chain",
               "All four copy directions execute; the result is bound and sampled.",
               "textured-quad.png", true),
    new Sample("compute", "Compute / 03", "Compute handoff",
               "Per-dispatch constants animate a compute-generated vertex stream.",
               "compute.png", true),
    new Sample("indexed-depth", "Render / 04", "Indexed depth",
               "Indexed depth, occlusion, barriers, and depth preview.",
               "indexed-depth.png", true),
    new Sample("instancing", "Render / 05", "Instancing",
               "Instance-rate vertices and dynamic uniform offsets.",
               "instancing.png", true),
    new Sample("textured-cube", "Render / 06", "Rotating cube",
               "Texture sampling, indexed draw, and depth.",
               "textured-cube.png", true),
    new Sample("storage-texture", "Compute / 07", "Storage texture",
               "Compute writes typed RGBA8 pixels; render samples them.",
               "storage-texture.png", true),
    new Sample("push-constants", "Render / 08", "Push constants",
               "Per-draw constants through the native Vulkan path.",
               "push-constants.png", true),
    new Sample("msaa", "Render / 09", "MSAA resolve",
               "Four-sample rasterization resolved into the swapchain.",
               "msaa.png", true),
    new Sample("mrt-blend", "Render / 10", "MRT blend",
               "Two color targets use independent alpha and additive blending.",
               "mrt-blend.png", true),
    new Sample("shadow-compare", "Binding / 11", "Comparison shadow",
               "Depth comparison sampling and filtered shadow visibility.",
               "shadow-compare.png", true),
    new Sample("texture-array", "Binding / 12", "Texture array",
               "Compute transforms layered textures before render samples them.",
               "texture-array.png", true),
    new Sample("texture-line", "Binding / 13", "Texture line",
               "Compute transforms a logical 1D texture before render.",
               "texture-line.png", true),
    new Sample("texture-shapes", "Binding / 14", "Texture shapes",
               "Cubemap faces and a 3D volume share one bind group.",
               "texture-shapes.png", true),
    new Sample("descriptor-array", "Binding / 15", "Descriptor arrays",
               "Dynamic texture, sampler, and buffer indices.",
               "descriptor-array.png", true),
    new Sample("msaa-samples", "Render / 16", "MSAA resolve + samples",
               "Hardware resolve and four explicit sample reads.",
               "msaa-samples.png", true),
    new Sample("subgroup", "Compute / 17", "Subgroup exchange",
               "A subgroup shuffle colors a compute-generated vertex stream.",
               "subgroup.png", true),
    new Sample("shader-f16", "Compute / 18", "Half precision",
               "Native f16 arithmetic with a bounded f32 fallback.",
               "shader-f16.png", true),
    new Sample("multi-draw", "Render / 19", "Multi-draw batch",
               "One API call consumes two packed indirect draw records.",
               "multi-draw.png", true),
    new Sample("dispatch-indirect", "Compute / 20", "Indirect dispatch",
               "A packed record launches the compute-to-render path.",
               "dispatch-indirect.png", true),
    new Sample("timestamp-query", "Compute / 21", "Pass timestamps",
               "Pass timestamps validate portable resolve alignment.",
               "timestamp-query.png", true),
    new Sample("compute-particles", "Compute / 22", "Compute particles",
               "Compute updates particles consumed directly by render.",
               "compute-particles.png", true),
    new Sample("mip-lod", "Binding / 23", "Mip chain + LOD",
               "One uploaded mip chain is sampled at five explicit levels.",
               "mip-lod.png", true),
    new Sample("stencil-outline", "Render / 24", "Stencil outline",
               "A fill pass writes stencil before the outline draw.",
               "stencil-outline.png", true),
    new Sample("bloom", "Compute / 25", "Separable bloom",
               "Horizontal and vertical compute feed a sampled composite.",
               "bloom.png", true),
    new Sample("image-texture", "Binding / 26", "Image texture",
               "Decoded sRGB pixels are uploaded and sampled once.",
               "image-texture.png", true),
    new Sample("integer-cube", "Binding / 27", "Integer cubemap",
               "Typed integer cube sampling through portable bindings.",
               "integer-cube.png", true),
    new Sample("color-pipeline", "Render / 28", "Color pipeline",
               "sRGB and linear input flow through HDR tone mapping.",
               "color-pipeline.png", true),
    new Sample("compressed-texture", "Binding / 29", "Compressed texture",
               "Native compression with a portable RGBA8 fallback.",
               "compressed-texture.png", true),
    new Sample("skinning", "Render / 30", "GPU skinning",
               "Four bone matrices animate an indexed mesh.",
               "skinning.png", true),
    new Sample("pbr-material", "Render / 31", "PBR material",
               "Metallic-roughness, normal maps, and environment lighting.",
               "pbr-material.png", true),
    new Sample("assetkit-damaged-helmet", "Render / 32",
               "AssetKit DamagedHelmet",
               "Khronos glTF, AssetKit materials, and indexed PBR rendering.",
               "assetkit-damaged-helmet.png", true),
    new Sample("blit", "Blit / 33", "Texture blit",
               "Nearest and linear magnification from one source texture.",
               "blit.png", true)
  };

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
    for (int i = 0; i < SAMPLES.length; i++) {
      page.addView(card(SAMPLES[i]), margin(0, i == 0 ? 0 : 18, 0, 0));
    }
    scroll.addView(page);
    return scroll;
  }

  private View card(Sample sample) {
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
        if (!sample.available) {
          Toast.makeText(GalleryActivity.this,
                         "Native Android port pending",
                         Toast.LENGTH_SHORT).show();
          return;
        }
        Intent intent = new Intent(GalleryActivity.this,
                                   SampleActivity.class);
        intent.putExtra("sample", sample.id);
        startActivity(intent);
      }
    });

    image.setImageBitmap(load("previews/" + sample.preview));
    image.setScaleType(ImageView.ScaleType.CENTER_CROP);
    image.setBackground(panel(12, Color.rgb(3, 8, 23), LINE));
    image.setClipToOutline(true);
    card.addView(image, new LinearLayout.LayoutParams(
      ViewGroup.LayoutParams.MATCH_PARENT,
      dp(206)
    ));
    card.addView(text(sample.category +
                      (sample.available ? "  /  READY" : "  /  PORTING"),
                      11,
                      ACCENT,
                      true),
                 margin(2, 14, 0, 0));
    card.addView(text(sample.title, 22, PRIMARY, true),
                 margin(2, 5, 0, 0));
    card.addView(text(sample.detail, 14, SECONDARY, false),
                 margin(2, 5, 0, 0));
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

  private static final class Sample {
    final String  id;
    final String  category;
    final String  title;
    final String  detail;
    final String  preview;
    final boolean available;

    Sample(String id,
           String category,
           String title,
           String detail,
           String preview,
           boolean available) {
      this.id        = id;
      this.category  = category;
      this.title     = title;
      this.detail    = detail;
      this.preview   = preview;
      this.available = available;
    }
  }
}
