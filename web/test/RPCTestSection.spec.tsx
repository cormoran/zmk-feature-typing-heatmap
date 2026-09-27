import { StrictMode } from "react";
import { act, render, screen, waitFor } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import {
  createConnectedMockZMKApp,
  ZMKAppProvider,
} from "@cormoran/zmk-studio-react-hook/testing";
import { TypingHeatmapSection } from "../src/TypingHeatmapSection";
import {
  Request,
  Response,
  StatsResponse,
} from "../src/proto/cormoran/feature-typing-heatmap/feature_typing_heatmap";
import { readHeatmap } from "../src/heatmap";

jest.mock("@zmkfirmware/zmk-studio-ts-client", () => ({
  create_rpc_connection: jest.fn(),
  call_rpc: jest.fn(),
  MetaError: class extends Error {},
}));
import { call_rpc } from "@zmkfirmware/zmk-studio-ts-client";
const rpc = call_rpc as jest.Mock;
const stats = (overrides: Partial<StatsResponse> = {}) =>
  StatsResponse.create({
    counts: [4, 8],
    positionCount: 2,
    persistenceEnabled: true,
    persistenceSupported: true,
    saveIntervalSeconds: 1800,
    minPresses: 100,
    unsavedPresses: 12,
    generation: 1,
    ...overrides,
  });

function setup(
  handler: (request: Request) => Response | Promise<Response> = () => ({
    stats: stats(),
  })
) {
  const requests: Request[] = [];
  rpc.mockImplementation(
    async (
      _connection: unknown,
      request: { core?: unknown; custom?: { call: { payload: Uint8Array } } }
    ) => {
      if (request.core) return { core: { getLockState: 1 } }; // Locked: module is unsecured.
      if (!request.custom) throw new Error("Unexpected RPC");
      const decoded = Request.decode(request.custom.call.payload);
      requests.push(decoded);
      const response = await handler(decoded);
      return {
        custom: {
          call: {
            payload: Response.encode(Response.create(response)).finish(),
          },
        },
      };
    }
  );
  const app = createConnectedMockZMKApp({
    subsystems: ["cormoran_typing_heatmap"],
  });
  const findSubsystem = app.findSubsystem;
  app.findSubsystem = (identifier) => {
    const found = findSubsystem(identifier);
    return found ? { ...found } : null;
  };
  const view = render(
    <ZMKAppProvider value={app}>
      <TypingHeatmapSection />
    </ZMKAppProvider>
  );
  return { requests, app, ...view };
}

beforeEach(() => jest.clearAllMocks());

test("loads once despite new subsystem objects and state renders; refresh is explicit", async () => {
  const { requests, app, rerender } = setup();
  await screen.findByText("12", { selector: "strong" });
  expect(requests).toHaveLength(1);
  rerender(
    <ZMKAppProvider value={app}>
      <TypingHeatmapSection />
    </ZMKAppProvider>
  );
  await act(async () => {
    await Promise.resolve();
  });
  expect(requests).toHaveLength(1);
  await userEvent.click(screen.getByRole("button", { name: "Refresh" }));
  await waitFor(() => expect(requests).toHaveLength(2));
  expect(screen.getByLabelText("Position 1: 8 presses")).toBeInTheDocument();
});

test("reset requires confirmation and reloads after mutation", async () => {
  let reset = false;
  const { requests } = setup((request) => {
    if (request.reset) {
      reset = true;
      return { mutation: { generation: 2, persistenceEnabled: true } };
    }
    return { stats: stats({ counts: reset ? [0, 0] : [4, 8] }) };
  });
  await screen.findByTestId("total-presses");
  await userEvent.click(
    screen.getByRole("button", { name: "Reset statistics" })
  );
  expect(requests).toHaveLength(1);
  await userEvent.click(screen.getByRole("button", { name: "Confirm reset" }));
  await waitFor(() =>
    expect(screen.getByTestId("total-presses")).toHaveTextContent("0")
  );
  expect(requests.map((r) => !!r.reset)).toEqual([false, true, false]);
});

test("mode toggle reloads memory policy and reports storage failures without losing visible counts", async () => {
  let enabled = true;
  setup((request) => {
    if (request.setPersistence) {
      enabled = request.setPersistence.enabled;
      return { mutation: { persistenceEnabled: enabled, generation: 2 } };
    }
    if (request.reset) return { error: { message: "Storage write failed" } };
    return { stats: stats({ persistenceEnabled: enabled }) };
  });
  const checkbox = await screen.findByRole("checkbox");
  await userEvent.click(checkbox);
  await screen.findByText(/Memory only:/);
  expect(checkbox).not.toBeChecked();
  await userEvent.click(
    screen.getByRole("button", { name: "Reset statistics" })
  );
  await userEvent.click(screen.getByRole("button", { name: "Confirm reset" }));
  expect(await screen.findByRole("alert")).toHaveTextContent(
    "Storage write failed"
  );
  expect(screen.getByTestId("total-presses")).toHaveTextContent("12");
});

test("storage unsupported disables persistence", async () => {
  setup(() => ({
    stats: stats({ persistenceSupported: false, persistenceEnabled: false }),
  }));
  expect(await screen.findByRole("checkbox")).toBeDisabled();
  expect(
    screen.getByText(/Persistent storage is unavailable/)
  ).toBeInTheDocument();
});

test("overlapping actions are disabled and stale connection completion is ignored", async () => {
  let finish!: (response: Response) => void;
  const { app, rerender } = setup(
    () =>
      new Promise((resolve) => {
        finish = resolve;
      })
  );
  await screen.findByRole("status");
  expect(screen.getByRole("button", { name: "Refresh" })).toBeDisabled();
  const next = createConnectedMockZMKApp({
    subsystems: ["cormoran_typing_heatmap"],
  });
  // Change the actual connection rather than only replacing the context wrapper.
  expect(next.state.connection).not.toBe(app.state.connection);
  rpc.mockImplementation(async (_c: unknown, request: { core?: unknown }) =>
    request.core
      ? { core: { getLockState: 1 } }
      : {
          custom: {
            call: {
              payload: Response.encode({
                stats: stats({ counts: [1, 1] }),
              }).finish(),
            },
          },
        }
  );
  rerender(
    <ZMKAppProvider value={next}>
      <TypingHeatmapSection />
    </ZMKAppProvider>
  );
  await waitFor(() =>
    expect(screen.getByTestId("total-presses")).toHaveTextContent("2")
  );
  await act(async () => finish({ stats: stats({ counts: [99, 99] }) }));
  expect(screen.getByTestId("total-presses")).toHaveTextContent("2");
});

test("pagination restarts on generation mismatch and returns only coherent counts", async () => {
  const pages = [
    stats({ positionCount: 18, counts: Array(16).fill(1) }),
    stats({ positionCount: 18, offset: 16, counts: [2, 2], generation: 2 }),
    stats({ positionCount: 18, counts: Array(16).fill(3), generation: 3 }),
    stats({ positionCount: 18, offset: 16, counts: [4, 4], generation: 3 }),
  ];
  const call = jest.fn(async () => ({ stats: pages.shift()! }));
  const result = await readHeatmap(call);
  expect(result.counts).toEqual([...Array(16).fill(3), 4, 4]);
  expect(call.mock.calls).toHaveLength(4);
});

test("continuous typing retries are bounded and invalid pages fail clearly", async () => {
  let calls = 0;
  const call = jest.fn(async (request: Request) => ({
    stats: stats({
      positionCount: 18,
      counts: request.getStats?.offset ? [1, 1] : Array(16).fill(1),
      offset: request.getStats?.offset,
      generation: ++calls,
    }),
  }));
  await expect(readHeatmap(call)).rejects.toThrow("Pause typing briefly");
  expect(call).toHaveBeenCalledTimes(6);
  await expect(
    readHeatmap(async () => ({ stats: stats({ counts: [] }) }))
  ).rejects.toThrow("invalid statistics page");
});

test("successful mutation invalidates old snapshot if reload fails", async () => {
  let changed = false;
  setup((request) => {
    if (request.reset) {
      changed = true;
      return { mutation: { generation: 2, persistenceEnabled: true } };
    }
    if (changed) throw new Error("Read timed out");
    return { stats: stats() };
  });
  await screen.findByTestId("total-presses");
  await userEvent.click(
    screen.getByRole("button", { name: "Reset statistics" })
  );
  await userEvent.click(screen.getByRole("button", { name: "Confirm reset" }));
  expect(await screen.findByRole("alert")).toHaveTextContent(
    "Operation succeeded, but refreshing statistics failed"
  );
  expect(screen.queryByTestId("total-presses")).not.toBeInTheDocument();
});

test("StrictMode initial loader performs one fetch", async () => {
  const { app, unmount } = setup();
  unmount();
  rpc.mockClear();
  render(
    <StrictMode>
      <ZMKAppProvider value={app}>
        <TypingHeatmapSection />
      </ZMKAppProvider>
    </StrictMode>
  );
  await screen.findByTestId("total-presses");
  expect(rpc.mock.calls.filter(([, request]) => request.custom)).toHaveLength(
    1
  );
});
