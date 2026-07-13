import SwiftUI

struct ContentView: View {
  @ObservedObject var controller: FabricController

  private var statusColor: Color {
    switch controller.linkState {
    case .connected: .green
    case .connecting: .orange
    case .waiting: .secondary
    case .unavailable: .red
    }
  }

  var body: some View {
    VStack(spacing: 0) {
      header
      Divider()
      runtime
      Divider()
      activity
    }
    .frame(minWidth: 560, minHeight: 460)
    .background(Color(nsColor: .windowBackgroundColor))
  }

  private var header: some View {
    HStack(spacing: 12) {
      Image(systemName: "point.3.connected.trianglepath.dotted")
        .font(.title2)
        .foregroundStyle(.orange)
      VStack(alignment: .leading, spacing: 2) {
        Text("TumoFabric Mac")
          .font(.headline)
        Text(controller.linkState.title)
          .font(.caption)
          .foregroundStyle(.secondary)
          .lineLimit(1)
      }
      Spacer()
      Circle()
        .fill(statusColor)
        .frame(width: 10, height: 10)
      Button {
        controller.refresh()
      } label: {
        Image(systemName: "arrow.clockwise")
      }
      .buttonStyle(.borderless)
      .help("Refresh")
    }
    .padding(.horizontal, 20)
    .padding(.vertical, 14)
  }

  private var runtime: some View {
    VStack(spacing: 20) {
      HStack {
        Label(
          controller.snapshot.active ? "Session active" : "Session idle",
          systemImage: controller.snapshot.active ? "bolt.horizontal.fill" : "pause.fill"
        )
        .foregroundStyle(controller.snapshot.active ? .primary : .secondary)
        Spacer()
        Text(controller.snapshot.owner.uppercased())
          .font(.caption.monospaced())
          .foregroundStyle(.secondary)
        Text("SEQ \(controller.snapshot.sequence)")
          .font(.caption.monospacedDigit())
          .foregroundStyle(.secondary)
      }

      Text(controller.snapshot.value.formatted())
        .font(.system(size: 76, weight: .medium, design: .rounded))
        .monospacedDigit()
        .contentTransition(.numericText())
        .frame(maxWidth: .infinity, minHeight: 92)

      HStack(spacing: 16) {
        Button {
          controller.decrement()
        } label: {
          Image(systemName: "minus")
            .frame(width: 52, height: 28)
        }
        .help("Decrease counter")
        .disabled(!controller.snapshot.active || !controller.isConnected)

        Button {
          controller.increment()
        } label: {
          Image(systemName: "plus")
            .frame(width: 52, height: 28)
        }
        .help("Increase counter")
        .disabled(!controller.snapshot.active || !controller.isConnected)
      }
      .buttonStyle(.borderedProminent)

      HStack(spacing: 10) {
        Button("Start", systemImage: "play.fill") { controller.start() }
          .disabled(controller.snapshot.active || !controller.isConnected)
        Button("Cancel", systemImage: "xmark") { controller.cancel() }
          .disabled(!controller.snapshot.active || !controller.isConnected)
        Spacer()
        Button("Export Trace", systemImage: "square.and.arrow.up") {
          controller.exportTrace()
        }
        .disabled(!controller.isConnected)
      }
      .buttonStyle(.bordered)
    }
    .padding(20)
  }

  private var activity: some View {
    VStack(alignment: .leading, spacing: 8) {
      Text("Activity")
        .font(.caption.weight(.semibold))
        .foregroundStyle(.secondary)
      ScrollView {
        LazyVStack(alignment: .leading, spacing: 5) {
          if controller.activity.isEmpty {
            Text("No events")
              .foregroundStyle(.tertiary)
          } else {
            ForEach(Array(controller.activity.suffix(20).enumerated()), id: \.offset) {
              _, event in
              Text(event)
                .font(.caption.monospaced())
                .textSelection(.enabled)
            }
          }
        }
        .frame(maxWidth: .infinity, alignment: .leading)
      }
    }
    .padding(.horizontal, 20)
    .padding(.vertical, 14)
    .frame(maxHeight: .infinity, alignment: .top)
  }
}
