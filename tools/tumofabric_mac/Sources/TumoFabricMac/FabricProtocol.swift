import Foundation

enum FabricCommand: Equatable {
  case capabilities
  case state
  case start
  case increment
  case decrement
  case cancel
  case trace

  var wireValue: String {
    switch self {
    case .capabilities: "tumofabric caps"
    case .state: "tumofabric state"
    case .start: "tumofabric start"
    case .increment: "tumofabric step inc"
    case .decrement: "tumofabric step dec"
    case .cancel: "tumofabric cancel"
    case .trace: "tumofabric trace"
    }
  }
}

struct FabricSnapshot: Equatable {
  let active: Bool
  let owner: String
  let sequence: UInt32
  let value: Int
  let persistence: String
}

struct FabricResponse: Equatable {
  let fields: [String: String]
  let trace: String?

  init(line: String) throws {
    let normalized = line.trimmingCharacters(in: .whitespacesAndNewlines)
    guard normalized.hasPrefix("FABRIC ") else {
      throw FabricClientError.invalidResponse
    }

    var payload = String(normalized.dropFirst("FABRIC ".count))
    var rawTrace: String?
    if let traceRange = payload.range(of: ";trace=") {
      rawTrace = String(payload[traceRange.upperBound...])
      payload = String(payload[..<traceRange.lowerBound])
    }

    var parsed: [String: String] = [:]
    for pair in payload.split(separator: ";", omittingEmptySubsequences: false) {
      guard let separator = pair.firstIndex(of: "=") else {
        throw FabricClientError.invalidResponse
      }
      let key = String(pair[..<separator])
      let value = String(pair[pair.index(after: separator)...])
      guard !key.isEmpty, !value.isEmpty, parsed[key] == nil else {
        throw FabricClientError.invalidResponse
      }
      parsed[key] = value
    }

    guard parsed["schema"] == "1" else {
      throw FabricClientError.unsupportedSchema
    }
    if parsed["status"] == "error" {
      throw FabricClientError.device(parsed["error"] ?? "unknown")
    }
    guard parsed["status"] == "ok" else {
      throw FabricClientError.invalidResponse
    }

    fields = parsed
    trace = rawTrace
  }

  var snapshot: FabricSnapshot? {
    guard
      let activeText = fields["active"],
      let active = Int(activeText),
      active == 0 || active == 1,
      let owner = fields["owner"]
    else {
      return nil
    }

    if active == 0 {
      return FabricSnapshot(
        active: false,
        owner: "none",
        sequence: 0,
        value: 0,
        persistence: fields["persist"] ?? "ram"
      )
    }

    guard
      let sequenceText = fields["seq"],
      let sequence = UInt32(sequenceText),
      let valueText = fields["value"],
      let value = Int(valueText),
      (-999...999).contains(value)
    else {
      return nil
    }

    return FabricSnapshot(
      active: true,
      owner: owner,
      sequence: sequence,
      value: value,
      persistence: fields["persist"] ?? "ram"
    )
  }

  var supportsOperatorPlane: Bool {
    guard fields["node"] == "flipper", fields["transport"] == "usb" else {
      return false
    }
    let operations = Set((fields["ops"] ?? "").split(separator: ",").map(String.init))
    return operations.isSuperset(of: ["state", "start", "inc", "dec", "cancel", "trace"])
  }
}

enum FabricClientError: LocalizedError, Equatable {
  case noDevice
  case portBusy
  case disconnected
  case timeout
  case invalidResponse
  case unsupportedSchema
  case unsupportedFirmware
  case device(String)
  case system(Int32)

  var errorDescription: String? {
    switch self {
    case .noDevice: "Waiting for Flipper USB"
    case .portBusy: "USB is in use by qFlipper or another app"
    case .disconnected: "Flipper disconnected"
    case .timeout: "Flipper did not answer"
    case .invalidResponse: "Invalid TumoFabric response"
    case .unsupportedSchema: "Unsupported TumoFabric schema"
    case .unsupportedFirmware: "TumoFabric USB requires firmware 037-032 or newer"
    case .device(let message): "Flipper: \(message)"
    case .system(let code): "USB error \(code)"
    }
  }
}
