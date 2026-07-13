import CTumoFabricSerial
import Darwin
import Foundation

actor USBSerialTransport {
  private var descriptor: Int32 = -1
  private(set) var connectedPort: String?

  static func discoverPort() -> String? {
    guard let entries = try? FileManager.default.contentsOfDirectory(atPath: "/dev") else {
      return nil
    }
    return
      entries
      .filter { $0.hasPrefix("cu.usbmodemflip_") }
      .sorted()
      .first
      .map { "/dev/\($0)" }
  }

  func connect(to port: String) throws {
    if connectedPort == port, descriptor >= 0 { return }
    disconnect()

    errno = 0
    let opened = port.withCString { tf_serial_open($0) }
    guard opened >= 0 else {
      if errno == EBUSY || errno == EACCES { throw FabricClientError.portBusy }
      throw FabricClientError.system(errno)
    }

    descriptor = opened
    connectedPort = port
    do {
      _ = try exchange("")
    } catch {
      disconnect()
      throw error
    }
  }

  func disconnect() {
    if descriptor >= 0 { _ = tf_serial_close(descriptor) }
    descriptor = -1
    connectedPort = nil
  }

  func execute(_ command: FabricCommand) throws -> FabricResponse {
    let output = try exchange(command.wireValue)
    guard
      let line =
        output
        .split(whereSeparator: \.isNewline)
        .map(String.init)
        .first(where: { $0.hasPrefix("FABRIC ") })
    else {
      throw FabricClientError.invalidResponse
    }
    return try FabricResponse(line: line)
  }

  private func exchange(_ command: String) throws -> String {
    guard descriptor >= 0 else { throw FabricClientError.disconnected }
    let wire = command + "\r"
    let writeResult = wire.withCString { pointer in
      tf_serial_write_all(descriptor, pointer, strlen(pointer))
    }
    guard writeResult == wire.utf8.count else {
      disconnect()
      throw FabricClientError.disconnected
    }

    var buffer = [CChar](repeating: 0, count: 4096)
    let count = tf_serial_read_until(descriptor, &buffer, buffer.count, ">: ", 1800)
    if count == -2 { throw FabricClientError.timeout }
    guard count > 0 else {
      disconnect()
      throw FabricClientError.disconnected
    }
    return String(decoding: buffer.prefix(Int(count)).map(UInt8.init(bitPattern:)), as: UTF8.self)
  }
}
