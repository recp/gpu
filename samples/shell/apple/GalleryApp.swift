import SwiftUI

#if os(macOS)
import AppKit
#endif

enum MetalMode: String, CaseIterable, Identifiable {
  case auto
  case classic
  case metal4

  var id: Self { self }

  var title: String {
    switch self {
    case .auto:
      return "Auto"
    case .classic:
      return "Classic"
    case .metal4:
      return "Metal 4"
    }
  }
}

struct GallerySample: Codable, Identifiable {
  let id: String
  let category: String
  let title: String
  let detail: String
  let preview: String
}

@MainActor
final class GalleryStore: ObservableObject {
  @Published private(set) var samples: [GallerySample] = []
  @Published private(set) var launchingSampleID: String?
  @Published var errorMessage: String?
  @Published var metalMode: MetalMode = .auto

#if os(macOS)
  private var activeApplication: NSRunningApplication?
  private var activeSampleID: String?
#endif

  init() {
    guard let url = Bundle.main.url(forResource: "catalog",
                                    withExtension: "json") else {
      errorMessage = "Gallery catalog is missing."
      return
    }

    do {
      let data = try Data(contentsOf: url, options: .mappedIfSafe)
      samples = try JSONDecoder().decode([GallerySample].self, from: data)
    } catch {
      errorMessage = "Gallery catalog could not be loaded."
    }
  }

  func isAvailable(_ sample: GallerySample) -> Bool {
    guard let path = NativeSamples.paths[sample.id] else {
      return false
    }
    return FileManager.default.isExecutableFile(atPath: path)
  }

  func launch(_ sample: GallerySample) {
#if os(macOS)
    guard launchingSampleID == nil else {
      return
    }
    guard let path = NativeSamples.paths[sample.id],
          FileManager.default.isExecutableFile(atPath: path) else {
      errorMessage = "\(sample.title) is not native on Metal yet."
      return
    }

    let executable = URL(fileURLWithPath: path)
    let bundle = executable
      .deletingLastPathComponent()
      .deletingLastPathComponent()
      .deletingLastPathComponent()
    var environment = ProcessInfo.processInfo.environment

    if activeSampleID == sample.id,
       let application = activeApplication,
       !application.isTerminated {
      application.activate(options: [.activateAllWindows])
      return
    }

    errorMessage = nil
    activeApplication?.terminate()
    activeApplication = nil
    activeSampleID = nil
    launchingSampleID = sample.id

    environment["GPU_METAL_MODE"] = metalMode.rawValue
    let configuration = NSWorkspace.OpenConfiguration()
    configuration.activates = true
    configuration.createsNewApplicationInstance = true
    configuration.environment = environment
    NSWorkspace.shared.openApplication(at: bundle,
                                       configuration: configuration) {
      [weak self] application, error in
      Task { @MainActor in
        guard let self else {
          return
        }

        self.launchingSampleID = nil
        guard let application, error == nil else {
          self.errorMessage = "\(sample.title) could not be launched."
          return
        }

        self.activeApplication = application
        self.activeSampleID = sample.id
        application.activate(options: [.activateAllWindows])
      }
    }
#else
    errorMessage = "\(sample.title) is not connected to the Apple runner yet."
#endif
  }
}

struct GalleryCard: View {
  let sample: GallerySample
  let available: Bool
  let launching: Bool
  let action: () -> Void

  var body: some View {
    Button(action: action) {
      VStack(alignment: .leading, spacing: 0) {
        preview
          .aspectRatio(1.6, contentMode: .fit)
          .frame(maxWidth: .infinity)
          .background(Color(red: 0.01, green: 0.03, blue: 0.09))
          .clipShape(RoundedRectangle(cornerRadius: 14,
                                      style: .continuous))
          .overlay {
            RoundedRectangle(cornerRadius: 14, style: .continuous)
              .stroke(Color.white.opacity(0.12), lineWidth: 1)
          }
          .overlay {
            if launching {
              ZStack {
                Color.black.opacity(0.46)
                ProgressView()
                  .controlSize(.large)
              }
              .clipShape(RoundedRectangle(cornerRadius: 14,
                                          style: .continuous))
            }
          }

        Text(sample.category.uppercased())
          .font(.system(size: 11, weight: .semibold, design: .monospaced))
          .foregroundStyle(Color(red: 1.0, green: 0.44, blue: 0.08))
          .padding(.top, 15)

        HStack(alignment: .firstTextBaseline) {
          Text(sample.title)
            .font(.system(size: 22, weight: .bold, design: .rounded))
            .foregroundStyle(.primary)

          Spacer(minLength: 8)

          Circle()
            .fill(available ? Color.green : Color.secondary.opacity(0.45))
            .frame(width: 7, height: 7)
        }
        .padding(.top, 6)

        Text(sample.detail)
          .font(.system(size: 14))
          .foregroundStyle(.secondary)
          .lineLimit(2, reservesSpace: true)
          .padding(.top, 6)
      }
      .padding(12)
      .background(Color(red: 0.055, green: 0.07, blue: 0.10))
      .clipShape(RoundedRectangle(cornerRadius: 19, style: .continuous))
      .overlay {
        RoundedRectangle(cornerRadius: 19, style: .continuous)
          .stroke(Color.white.opacity(0.11), lineWidth: 1)
      }
      .opacity(available ? 1.0 : 0.62)
      .contentShape(RoundedRectangle(cornerRadius: 19,
                                     style: .continuous))
    }
    .buttonStyle(.plain)
    .disabled(launching)
  }

  @ViewBuilder
  private var preview: some View {
#if os(macOS)
    if let url = Bundle.main.url(forResource: sample.id,
                                 withExtension: "png",
                                 subdirectory: "previews"),
       let image = NSImage(contentsOf: url) {
      Image(nsImage: image)
        .resizable()
        .scaledToFill()
        .clipped()
    } else {
      Color(red: 0.01, green: 0.03, blue: 0.09)
    }
#else
    Image(sample.id)
      .resizable()
      .scaledToFill()
      .clipped()
#endif
  }
}

struct GalleryView: View {
  @StateObject private var store = GalleryStore()

  private let columns = [
    GridItem(.adaptive(minimum: 300, maximum: 420), spacing: 22)
  ]

  var body: some View {
    ScrollView {
      VStack(alignment: .leading, spacing: 0) {
        header
        Divider()
          .overlay(Color.white.opacity(0.1))
          .padding(.top, 28)
          .padding(.bottom, 34)

        LazyVGrid(columns: columns, alignment: .leading, spacing: 24) {
          ForEach(store.samples) { sample in
            GalleryCard(sample: sample,
                        available: store.isAvailable(sample),
                        launching: store.launchingSampleID == sample.id) {
              store.launch(sample)
            }
          }
        }
      }
      .padding(.horizontal, 34)
      .padding(.top, 28)
      .padding(.bottom, 44)
    }
    .background(Color(red: 0.025, green: 0.03, blue: 0.04))
    .preferredColorScheme(.dark)
    .alert("GPU + USL Samples",
           isPresented: Binding(
             get: { store.errorMessage != nil },
             set: { if !$0 { store.errorMessage = nil } }
           )) {
      Button("OK", role: .cancel) {}
    } message: {
      Text(store.errorMessage ?? "")
    }
  }

  private var header: some View {
    HStack(spacing: 14) {
      Text("G")
        .font(.system(size: 16, weight: .black, design: .rounded))
        .foregroundStyle(Color(red: 0.03, green: 0.04, blue: 0.05))
        .frame(width: 38, height: 38)
        .background(Color(red: 1.0, green: 0.44, blue: 0.08))
        .clipShape(RoundedRectangle(cornerRadius: 9, style: .continuous))

      Text("GPU | Universal Shading (USL)")
        .font(.system(size: 14, weight: .bold))

      Spacer()

      Picker("Metal mode", selection: $store.metalMode) {
        ForEach(MetalMode.allCases) { mode in
          Text(mode.title).tag(mode)
        }
      }
      .labelsHidden()
      .pickerStyle(.menu)
      .frame(width: 96)

      Text("METAL SAMPLES")
        .font(.system(size: 11, weight: .medium, design: .monospaced))
        .foregroundStyle(.secondary)
        .tracking(1.2)
    }
  }
}

@main
struct GPUGalleryApp: App {
  var body: some Scene {
    WindowGroup("GPU + USL Samples") {
      GalleryView()
        .frame(minWidth: 760, minHeight: 620)
    }
    .defaultSize(width: 1240, height: 840)
  }
}
