import Foundation

enum NativeSamples {
  static let paths: [String: String] = {
    guard let url = Bundle.main.url(forResource: "native-samples",
                                    withExtension: "json"),
          let data = try? Data(contentsOf: url),
          let paths = try? JSONDecoder().decode([String: String].self,
                                                from: data) else {
      return [:]
    }

    return paths
  }()
}
