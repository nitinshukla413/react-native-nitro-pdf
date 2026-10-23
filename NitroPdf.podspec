require "json"

package = JSON.parse(File.read(File.join(__dir__, "package.json")))

Pod::Spec.new do |s|
  s.name         = "NitroPdf"
  s.version      = package["version"]
  s.summary      = package["description"]
  s.homepage     = package["homepage"]
  s.license      = package["license"]
  s.authors      = package["author"]

  s.platforms    = { :ios => min_ios_version_supported, :visionos => 1.0 }
  s.source       = { :git => "https://github.com/nitinshukla/react-native-nitro-pdf.git", :tag => "#{s.version}" }

  s.source_files = [
    # Platform glue (Objective-C++ font lookup, Swift PdfView)
    "ios/**/*.{m,mm,swift}",
    # Shared C++ implementation
    "cpp/*.{hpp,cpp}",
    "cpp/core/*.{hpp,cpp}",
    # Vendored litehtml + gumbo. Their headers sit next to the sources and stay out of
    # header maps, which are case-insensitive (gumbo "error.h" vs React "Error.h").
    "cpp/third_party/litehtml/include/litehtml/*.cpp",
    "cpp/third_party/litehtml/gumbo/include/gumbo/*.c",
  ]
  s.preserve_paths = [
    "cpp/third_party/**/*.h",
  ]

  litehtml = "$(PODS_TARGET_SRCROOT)/cpp/third_party/litehtml"
  s.pod_target_xcconfig = {
    "HEADER_SEARCH_PATHS" => "\"#{litehtml}/include\" \"#{litehtml}/gumbo/include\"",
  }
  s.frameworks = "CoreText", "PDFKit"
  s.libraries = "z"

  load 'nitrogen/generated/ios/NitroPdf+autolinking.rb'
  add_nitrogen_files(s)

  s.dependency 'React-jsi'
  s.dependency 'React-callinvoker'
  install_modules_dependencies(s)
end
