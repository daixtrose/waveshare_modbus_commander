# Service Interface Design Discussion

## Date: March 4–5, 2026

---

## 1. Original Requirements (User Prompt)

> I want to expose the functionality of waveshare_modbus_commander as a service. Normally I would go with OpenAPI, but some partners use asynchronous WebSocket connections. So I was wondering if the emerging AsyncAPI would be an option. What I would prefer is an AsyncAPI for the command line interface, implement that API in C++ using some open source library or Boost.Beast or something even better. Then implement a Java command line application that behaves identical to the C++ command line tool waveshare_modbus_commander, but calls the Async API and uses Reactive Extensions for Java to handle the asynchronous data flows.
>
> As an addon it would be nice to create a Flutter app that uses the same AsyncAPI.
>
> What I find important is the constraint that the WebSocket connection is tunneled via a TLS 1.3 connection with server and client certificates.
>
> Please note that fully automated code generation of client code and server stubs is crucial for me. This is why I am a huge fan of OpenAPI.

### Derived Hard Requirements

| # | Requirement |
|---|-------------|
| R1 | **Fully asynchronous, bidirectional** communication (no request/response coupling) |
| R2 | **C++ server** wrapping existing waveshare_modbus_commander logic |
| R3 | **Java CLI client** with RxJava / Reactive Extensions |
| R4 | **Flutter/Dart app** (addon) |
| R5 | **Mutual TLS 1.3** (server + client certificates) |
| R6 | **Automated code generation** of server stubs and client code from a single interface definition |

---

## 2. AsyncAPI Assessment

AsyncAPI (v3.0) is the right *conceptual* fit — it describes WebSocket channels, messages, and operations. However, **code generation maturity is the dealbreaker**:

| Target | AsyncAPI codegen status |
|--------|------------------------|
| C++ server stub | **Does not exist** |
| Java WebSocket client | Spring Cloud Stream only (Kafka/AMQP focus, not raw WebSocket) |
| Dart/Flutter client | **Does not exist** |
| mTLS configuration | Not generated |

The AsyncAPI generator (`@asyncapi/generator`) has ~5–6 templates, almost all Node.js or Spring Cloud Stream. There is no C++ template, no Dart template, and no raw-WebSocket Java client template. This violates requirement R6.

**Verdict: AsyncAPI fails the automated code generation requirement for the target languages.**

---

## 3. gRPC Bidirectional Streaming — Recommended Approach

### Why gRPC Fits

| Requirement | gRPC coverage |
|-------------|---------------|
| R1 — Fully async bidi | Bidirectional streaming RPCs: both sides send independently once stream is open |
| R2 — C++ server | Official `protoc` C++ codegen, production-grade |
| R3 — Java + RxJava | Official Java codegen + [rxgrpc](https://github.com/salesforce/reactive-grpc) wraps stubs in `Flowable`/`Single` |
| R4 — Flutter/Dart | Official `grpc` Dart package with `protoc-gen-dart` |
| R5 — mTLS TLS 1.3 | Built into `grpc::SslCredentials` / `SslServerCredentials` |
| R6 — Code generation | Single `.proto` file → C++, Java, Dart stubs fully generated |

### Is Async a First-Class Citizen in gRPC?

**It depends on the language:**

- **C++**: Callback-based API (`grpc::CallbackServerContext`) and experimental C++ coroutine support (gRPC ≥ 1.56). Functional but more boilerplate than other languages. The older `CompletionQueue` API is powerful but verbose.
- **Java**: `StreamObserver<T>` is inherently async. rxgrpc/reactor-grpc wrap it into `Flowable<ServerMessage>` / `Flux<ServerMessage>` — true reactive streams with backpressure. **Excellent fit.**
- **Dart/Flutter**: Returns native `Stream<ServerMessage>` for streaming RPCs — fully integrated with Dart's async model. **Excellent fit.**

### What gRPC Bidi Streaming Does NOT Give You

1. **Client must initiate the stream.** The server cannot push data until the client opens the bidi stream. Once open, both sides are fully independent.
2. **HTTP/2, not WebSocket.** Partners expecting a raw `ws://` or `wss://` endpoint won't get one. A gRPC-Web proxy (Envoy) or a thin bridge would be needed for browser clients.
3. **No topic/channel multiplexing within one stream.** Multiple independent subscriptions are handled via correlation IDs in the message envelope or by opening parallel streams.
4. **Binary on the wire (Protobuf).** Not human-readable like JSON. Tools like `grpcurl` / `grpcui` fill the debugging gap.

---

## 4. Alternative: Raw WebSocket with JSON (If WebSocket Is a Hard Requirement)

If partners specifically mandate WebSocket framing:

1. Define the API in **AsyncAPI** for documentation/contract purposes
2. Hand-implement the C++ server with **Boost.Beast** WebSocket + TLS 1.3
3. Use **JSON Schema** (embedded in AsyncAPI spec) to generate **message types** via `quicktype` (supports Java, Dart, C++)
4. Write thin WebSocket client wrappers manually in Java/Dart using the generated types

**Trade-off:** Spec-driven contract without full code generation — significantly more manual work than gRPC.

---

## 5. Comparison Matrix

| Criterion | AsyncAPI + WS | gRPC Bidi | OpenAPI + WS sidecar |
|-----------|---------------|-----------|---------------------|
| Fully async both directions | Yes | Yes (once stream open) | Bolted on |
| Server-initiated push | Yes | Yes (within stream) | Yes |
| C++ codegen | **None** | Excellent | REST only |
| Java + RxJava codegen | **None** (raw WS) | rxgrpc | REST only |
| Dart/Flutter codegen | **None** | Excellent | REST only |
| mTLS built-in | Manual | Built-in | Manual |
| Raw WebSocket wire format | Yes | No (HTTP/2) | Yes |
| Spec-as-contract | Yes | `.proto` | Yes |
| Ecosystem maturity | Early | **Mature** | Mature |
| Browser support | Native | Via Envoy proxy | Native |

---

## 6. Decision

**gRPC with bidirectional streaming** was chosen because it is the only option that satisfies all six requirements simultaneously. The `.proto` file serves the same single-source-of-truth role that an OpenAPI spec does for REST.

A `.proto` schema (`waveshare_commander.proto`) was created in this directory, mapping 1:1 to the existing CLI commands in `cli_parser.hpp` / `cli_parser.cpp`.

---

## 7. Architecture Overview

```
waveshare_commander.proto       ← single source of truth
         │
    protoc + language plugins
         │
    ┌────┴──────────────────────────────────┐
    │               │                        │
 C++ server     Java CLI client          Flutter app
 (gRPC server +   (rxgrpc stubs +        (grpc-dart +
  existing logic)   RxJava pipelines)      provider/bloc)
    │
 libmodbus-cpp
    │
 Modbus TCP → Waveshare device
```

### Proto Design: Single Bidi Stream with Envelope Multiplexing

```protobuf
service WaveshareCommander {
  rpc Session(stream ClientMessage) returns (stream ServerMessage);
}
```

- **`ClientMessage`** carries a `request_id` + `oneof command` (one variant per CLI action)
- **`ServerMessage`** echoes the `request_id` + `bool final` flag + `oneof response`
- One-shot commands (read coil, write register, etc.) produce a single response with `final = true`
- Streaming commands (iterate relays, scan network) produce multiple responses; `final = true` on the last one
- **`CancelRequest`** replaces Ctrl-C for streaming operations

### CLI → Proto Mapping

| CLI flag | ClientMessage.command | ServerMessage.response |
|----------|----------------------|------------------------|
| `-i -p -t` | `ConnectRequest` | `ConnectResponse` |
| `--read-coil` | `ReadCoilRequest` | `CoilState` |
| `--read-coils` | `ReadCoilsRequest` | `CoilStates` |
| `--write-coil` | `WriteCoilRequest` | `WriteResult` |
| `--write-coils` | `WriteCoilsRequest` | `WriteResult` |
| `--read-register` | `ReadRegisterRequest` | `RegisterValue` |
| `--read-registers` | `ReadRegistersRequest` | `RegisterValues` |
| `--write-register` | `WriteRegisterRequest` | `WriteResult` |
| `--write-registers` | `WriteRegistersRequest` | `WriteResult` |
| `--read-digital-inputs` | `ReadDigitalInputsRequest` | `DigitalInputsState` |
| `--iterate-relais-switches` | `IterateRelaySwitchesRequest` | N × `RelayEvent` |
| `--scan-network` | `ScanNetworkRequest` | N × `DeviceInfo` + `ScanComplete` |
| `--set-ip` | `SetStaticIpRequest` | `ConfigResult` → `DeviceReappeared` |
| `--set-dhcp` | `SetDhcpRequest` | `ConfigResult` → `DeviceReappeared` |
| `--set-modbus-tcp` | `SetModbusTcpRequest` | `ConfigResult` → `DeviceReappeared` |
| `--set-modbus-tcp-port` | `SetModbusTcpPortRequest` | `ConfigResult` → `DeviceReappeared` |
| `--set-name` | `SetNameRequest` | `ConfigResult` → `DeviceReappeared` |
| Ctrl-C | `CancelRequest` | `CancelAck` |

---

## 8. Summary for Future Prompts

**Context:** The `waveshare_modbus_commander` project (C++23, CMake, libmodbus-cpp) is being extended with a network service layer. After evaluating AsyncAPI (rejected due to missing C++/Dart code generators), **gRPC bidirectional streaming** was selected.

**Key artefact:** `proto/waveshare_commander.proto` — a single bidi-stream service (`Session`) with envelope-multiplexed commands/responses. Maps 1:1 to all CLI actions in `cli_parser.hpp`. Uses `request_id` correlation, a `final` flag for streaming termination, and `CancelRequest` for cancellation.

**Planned implementation targets:**
1. **C++ gRPC server** — wraps existing waveshare_modbus_commander logic, mTLS via `SslServerCredentials`
2. **Java CLI client** — generated stubs + rxgrpc for RxJava `Flowable`/`Single` pipelines, behaviorally identical to the C++ CLI
3. **Flutter app** — generated Dart stubs via `protoc-gen-dart`, async `Stream`-based UI

**Open decisions:**
- Whether to keep the `.proto` in `proto/` or move to `interface_definitions/` (user initially requested `interface_definitions/`, move was skipped)
- C++ async API choice: callback-based vs. coroutine-based gRPC
- Whether an Envoy gRPC-Web proxy is needed for browser-based Flutter web
- Build integration: how to wire `protoc` into the existing CMake build
