import AppKit
import Combine
import Foundation
import UniformTypeIdentifiers

@MainActor
final class FabricController: ObservableObject {
  enum LinkState: Equatable {
    case waiting
    case connecting
    case connected(String)
    case unavailable(String)

    var title: String {
      switch self {
      case .waiting: "Waiting for Flipper"
      case .connecting: "Connecting"
      case .connected: "USB connected"
      case .unavailable(let message): message
      }
    }
  }

  @Published private(set) var linkState: LinkState = .waiting
  @Published private(set) var snapshot = FabricSnapshot(
    active: false,
    owner: "none",
    sequence: 0,
    value: 0,
    persistence: "ram"
  )
  @Published private(set) var activity: [String] = []
  @Published private(set) var operationInFlight = false

  private let transport = USBSerialTransport()
  private var monitorTask: Task<Void, Never>?
  private var pollInFlight = false
  private var lastStatusActivity: String?

  init() {
    monitorTask = Task { [weak self] in
      while !Task.isCancelled {
        await self?.poll()
        try? await Task.sleep(for: .milliseconds(750))
      }
    }
  }

  deinit {
    monitorTask?.cancel()
  }

  var isConnected: Bool {
    if case .connected = linkState { return true }
    return false
  }

  func start() { perform(.start, label: "Started") }
  func increment() { perform(.increment, label: "+1") }
  func decrement() { perform(.decrement, label: "-1") }
  func cancel() { perform(.cancel, label: "Cancelled") }

  func refresh() {
    Task { await poll(force: true) }
  }

  func exportTrace() {
    guard isConnected, !operationInFlight else { return }
    operationInFlight = true
    Task {
      defer { operationInFlight = false }
      do {
        let response = try await transport.execute(.trace)
        guard let trace = response.trace else { throw FabricClientError.invalidResponse }
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "tumofabric-trace.txt"
        panel.allowedContentTypes = [.plainText]
        guard panel.runModal() == .OK, let url = panel.url else { return }
        try (trace + "\n").write(to: url, atomically: true, encoding: .utf8)
        appendActivity("Trace exported")
      } catch {
        handle(error)
      }
    }
  }

  func shutdown() {
    monitorTask?.cancel()
    monitorTask = nil
    Task { await transport.disconnect() }
  }

  private func perform(_ command: FabricCommand, label: String) {
    guard isConnected, !operationInFlight else { return }
    operationInFlight = true
    Task {
      defer { operationInFlight = false }
      do {
        let response = try await transport.execute(command)
        guard let next = response.snapshot else { throw FabricClientError.invalidResponse }
        snapshot = next
        appendActivity(label)
      } catch {
        handle(error)
      }
    }
  }

  private func poll(force: Bool = false) async {
    guard !pollInFlight, force || !operationInFlight else { return }
    pollInFlight = true
    defer { pollInFlight = false }

    do {
      if await transport.connectedPort == nil {
        guard let port = USBSerialTransport.discoverPort() else {
          linkState = .waiting
          lastStatusActivity = nil
          snapshot = FabricSnapshot(
            active: false,
            owner: "none",
            sequence: 0,
            value: 0,
            persistence: "ram"
          )
          return
        }
        if case .waiting = linkState { linkState = .connecting }
        try await transport.connect(to: port)
        let capabilities: FabricResponse
        do {
          capabilities = try await transport.execute(.capabilities)
        } catch FabricClientError.invalidResponse {
          throw FabricClientError.unsupportedFirmware
        }
        guard capabilities.supportsOperatorPlane else {
          throw FabricClientError.unsupportedFirmware
        }
        linkState = .connected(port)
        appendStatusActivity("USB connected")
      }

      let response = try await transport.execute(.state)
      guard let next = response.snapshot else { throw FabricClientError.invalidResponse }
      if next != snapshot, next.active {
        appendActivity("Counter \(next.value)")
      }
      snapshot = next
      if let port = await transport.connectedPort { linkState = .connected(port) }
    } catch {
      handle(error)
    }
  }

  private func handle(_ error: Error) {
    let message = (error as? LocalizedError)?.errorDescription ?? error.localizedDescription
    linkState = .unavailable(message)
    appendStatusActivity(message)
    Task { await transport.disconnect() }
  }

  private func appendStatusActivity(_ message: String) {
    guard message != lastStatusActivity else { return }
    lastStatusActivity = message
    appendActivity(message)
  }

  private func appendActivity(_ message: String) {
    let formatter = DateFormatter()
    formatter.dateFormat = "HH:mm:ss"
    activity.append("\(formatter.string(from: Date()))  \(message)")
    if activity.count > 80 { activity.removeFirst(activity.count - 80) }
  }
}
