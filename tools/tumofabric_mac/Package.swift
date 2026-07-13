// swift-tools-version: 5.10

import PackageDescription

let package = Package(
  name: "TumoFabricMac",
  platforms: [.macOS(.v14)],
  products: [
    .executable(name: "TumoFabricMac", targets: ["TumoFabricMac"])
  ],
  targets: [
    .target(
      name: "CTumoFabricSerial",
      publicHeadersPath: "include"
    ),
    .executableTarget(
      name: "TumoFabricMac",
      dependencies: ["CTumoFabricSerial"]
    ),
    .testTarget(
      name: "TumoFabricMacTests",
      dependencies: ["TumoFabricMac"]
    ),
  ]
)
