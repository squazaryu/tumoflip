import XCTest

@testable import TumoFabricMac

final class FabricProtocolTests: XCTestCase {
  func testCommandGrammarIsFixed() {
    XCTAssertEqual(FabricCommand.capabilities.wireValue, "tumofabric caps")
    XCTAssertEqual(FabricCommand.state.wireValue, "tumofabric state")
    XCTAssertEqual(FabricCommand.start.wireValue, "tumofabric start")
    XCTAssertEqual(FabricCommand.increment.wireValue, "tumofabric step inc")
    XCTAssertEqual(FabricCommand.decrement.wireValue, "tumofabric step dec")
    XCTAssertEqual(FabricCommand.cancel.wireValue, "tumofabric cancel")
    XCTAssertEqual(FabricCommand.trace.wireValue, "tumofabric trace")
  }

  func testParsesActiveSnapshot() throws {
    let response = try FabricResponse(
      line: "FABRIC schema=1;status=ok;active=1;owner=iphone;seq=7;value=-4;persist=ram"
    )
    XCTAssertEqual(
      response.snapshot,
      FabricSnapshot(active: true, owner: "iphone", sequence: 7, value: -4, persistence: "ram")
    )
  }

  func testValidatesBoundedOperatorCapabilities() throws {
    let valid = try FabricResponse(
      line:
        "FABRIC schema=1;status=ok;node=flipper;transport=usb;ops=state,start,inc,dec,cancel,trace;active=0;owner=none"
    )
    XCTAssertTrue(valid.supportsOperatorPlane)

    let incomplete = try FabricResponse(
      line:
        "FABRIC schema=1;status=ok;node=flipper;transport=usb;ops=state,start;active=0;owner=none"
    )
    XCTAssertFalse(incomplete.supportsOperatorPlane)
  }

  func testIdleSnapshotDoesNotRetainStaleValues() throws {
    let response = try FabricResponse(
      line: "FABRIC schema=1;status=ok;active=0;owner=none;seq=42;value=99;persist=ram"
    )
    XCTAssertEqual(
      response.snapshot,
      FabricSnapshot(active: false, owner: "none", sequence: 0, value: 0, persistence: "ram")
    )
  }

  func testParsesNestedTraceWithoutTreatingItAsCommands() throws {
    let response = try FabricResponse(
      line: "FABRIC schema=1;status=ok;trace=schema=1;depth=8;count=2;drop=0|r,f,o|t,f,o"
    )
    XCTAssertEqual(response.trace, "schema=1;depth=8;count=2;drop=0|r,f,o|t,f,o")
  }

  func testRejectsDeviceErrorsAndDuplicateFields() {
    XCTAssertThrowsError(
      try FabricResponse(line: "FABRIC schema=1;status=error;error=busy")
    ) { error in
      XCTAssertEqual(error as? FabricClientError, .device("busy"))
    }
    XCTAssertThrowsError(
      try FabricResponse(line: "FABRIC schema=1;status=ok;status=ok")
    )
  }

  func testRejectsUnsupportedSchemaAndOutOfRangeCounter() throws {
    XCTAssertThrowsError(
      try FabricResponse(line: "FABRIC schema=2;status=ok;active=0;owner=none")
    )
    let response = try FabricResponse(
      line: "FABRIC schema=1;status=ok;active=1;owner=flipper;seq=1;value=1000;persist=ram"
    )
    XCTAssertNil(response.snapshot)
  }
}
