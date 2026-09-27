import { useCallback, useContext, useEffect, useRef, useState } from "react";
import {
  ZMKAppContext,
  useCustomSubsystem,
  useStudioLockState,
  isUnlockRequiredError,
} from "@cormoran/zmk-studio-react-hook";
import { Request } from "./proto/cormoran/feature-typing-heatmap/feature_typing_heatmap";
import {
  HEATMAP_CODEC,
  type HeatmapSnapshot,
  readHeatmap,
  requireResponse,
  exportHeatmap,
} from "./heatmap";

const SUBSYSTEM_IDENTIFIER = "cormoran_typing_heatmap";

export function TypingHeatmapSection() {
  const app = useContext(ZMKAppContext);
  const connection = app?.state.connection;
  const { ready, subsystem, call } = useCustomSubsystem(
    SUBSYSTEM_IDENTIFIER,
    HEATMAP_CODEC
  );
  const subsystemIndex = subsystem?.index;
  const { locked } = useStudioLockState();
  const [snapshot, setSnapshot] = useState<HeatmapSnapshot | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [awaitingUnlock, setAwaitingUnlock] = useState(false);
  const [confirmReset, setConfirmReset] = useState(false);
  const session = useRef(0);
  const inFlight = useRef(false);
  const retryRead = useRef(false);
  const previouslyLocked = useRef(locked);

  const perform = useCallback(
    async (mutation?: Request) => {
      if (!ready || inFlight.current) return;
      const identity = session.current;
      inFlight.current = true;
      setBusy(true);
      setError(null);
      setAwaitingUnlock(false);
      let mutationSucceeded = false;
      try {
        if (mutation) {
          const result = requireResponse(await call(mutation));
          if (!result.mutation)
            throw new Error("Unexpected mutation response.");
          mutationSucceeded = true;
          if (identity === session.current) setSnapshot(null);
        }
        if (identity !== session.current) return;
        const next = await readHeatmap(call);
        if (identity === session.current) {
          setSnapshot(next);
          retryRead.current = false;
        }
      } catch (failure) {
        if (identity !== session.current) return;
        if (isUnlockRequiredError(failure)) {
          retryRead.current = !mutation;
          setAwaitingUnlock(true);
        } else {
          const message =
            failure instanceof Error ? failure.message : "Request failed.";
          setError(
            mutationSucceeded
              ? `Operation succeeded, but refreshing statistics failed: ${message}`
              : message
          );
        }
      } finally {
        if (identity === session.current) {
          inFlight.current = false;
          setBusy(false);
        }
      }
    },
    [ready, call]
  );

  useEffect(() => {
    const identity = ++session.current;
    inFlight.current = false;
    retryRead.current = false;
    // Connection changes reflect an external device, so clear the old device's view.
    // eslint-disable-next-line react-hooks/set-state-in-effect
    setSnapshot(null);
    setError(null);
    setAwaitingUnlock(false);
    setConfirmReset(false);
    setBusy(false);
    if (ready) {
      // StrictMode replays setup/cleanup in development. Start only if this
      // session survives that replay, as well as a same-tick disconnect.
      void Promise.resolve().then(() => {
        if (session.current === identity) void perform();
      });
    }
    return () => {
      session.current = identity + 1;
    };
  }, [connection, subsystemIndex, ready, perform]);

  useEffect(() => {
    const becameUnlocked = previouslyLocked.current && !locked;
    previouslyLocked.current = locked;
    if (awaitingUnlock && becameUnlocked && retryRead.current) {
      retryRead.current = false;
      void perform();
    }
  }, [awaitingUnlock, locked, perform]);

  if (!app) return null;
  if (subsystemIndex === undefined) {
    return (
      <section className="card">
        <p>
          Subsystem "{SUBSYSTEM_IDENTIFIER}" not found. Enable the typing
          heatmap module in your firmware.
        </p>
        <a
          href="https://github.com/cormoran/zmk-feature-typing-heatmap#readme"
          target="_blank"
          rel="noreferrer"
        >
          Module README
        </a>
      </section>
    );
  }

  const counts = snapshot?.counts ?? [];
  const total = counts.reduce((sum, count) => sum + count, 0);
  const max = Math.max(1, ...counts);

  return (
    <section className="card statistics" aria-busy={busy}>
      <h2>Typing heatmap</h2>
      <p>
        Counts by ZMK key position. This grid shows position indices, not your
        physical keyboard layout.
      </p>
      <div className="statistics-actions">
        <button
          className="btn btn-primary"
          disabled={busy || !ready}
          onClick={() => void perform()}
        >
          Refresh
        </button>
        <button
          className="btn"
          disabled={busy || !snapshot}
          onClick={() => snapshot && exportHeatmap(snapshot)}
        >
          Export JSON
        </button>
        <button
          className="btn"
          disabled={busy || !snapshot}
          onClick={() => setConfirmReset(true)}
        >
          Reset statistics
        </button>
      </div>
      {busy && <p role="status">Loading statistics…</p>}
      {error && (
        <p role="alert" className="error-message">
          {error}
        </p>
      )}
      {awaitingUnlock && (
        <div className="unlock-prompt">
          <p>
            Press <code>&amp;studio_unlock</code> on your keyboard to unlock
            Studio. Refresh retries the read; repeat a reset or mode change
            explicitly if needed.
          </p>
          <button
            className="btn"
            disabled={busy}
            onClick={() => void perform()}
          >
            Retry
          </button>
        </div>
      )}
      {snapshot && (
        <>
          <p className="total">
            Total presses:{" "}
            <strong data-testid="total-presses">
              {total.toLocaleString()}
            </strong>
          </p>
          <div className="heat-grid" aria-label="Key position counts">
            {counts.map((count, position) => (
              <div
                key={position}
                className="heat-cell"
                style={{
                  backgroundColor: `hsl(24 90% ${96 - (count / max) * 42}%)`,
                }}
                aria-label={`Position ${position}: ${count} presses`}
              >
                <span className="position">Position {position}</span>
                <strong>{count.toLocaleString()}</strong>
              </div>
            ))}
          </div>
          {counts.length === 0 && <p>No key positions are available.</p>}
          <label className="persistence-toggle">
            <input
              type="checkbox"
              checked={snapshot.persistenceEnabled}
              disabled={busy || !snapshot.persistenceSupported}
              onChange={(event) =>
                void perform({
                  setPersistence: { enabled: event.target.checked },
                })
              }
            />
            Save statistics across restarts
          </label>
          {!snapshot.persistenceSupported && (
            <p className="warning-message">
              Persistent storage is unavailable in this firmware. Statistics
              stay in memory and disappear on restart.
            </p>
          )}
          {snapshot.persistenceEnabled ? (
            <p>
              Automatic saving requires both {snapshot.saveIntervalSeconds}{" "}
              seconds since the last save attempt and at least{" "}
              {snapshot.minPresses} new presses.{" "}
              {snapshot.unsavedPresses.toLocaleString()} presses are unsaved.
              Unsaved counts are lost on power loss or restart.
            </p>
          ) : (
            <p>
              Memory only: all counts disappear on restart. Turning saving off
              clears the previous saved snapshot but keeps the current session
              counts.
            </p>
          )}
          <p className="hint-message">
            Turning saving off clears previously saved statistics and keeps
            session counts. Turning saving on starts a new save interval. Reset
            and mode changes write a small record immediately; automatic
            statistics saves wait for both limits. Refresh reads once; there is
            no continuous polling.
          </p>
          {snapshot.storageError !== 0 && (
            <p role="alert" className="error-message">
              Storage error ({snapshot.storageError}). Unsaved counts remain in
              memory. Automatic checkpoints retry at the configured interval
              while persistence is enabled.
            </p>
          )}
        </>
      )}
      {confirmReset && (
        <div
          className="reset-confirmation"
          role="group"
          aria-label="Confirm statistics reset"
        >
          <p>
            Clear all position counts and the saved snapshot? This cannot be
            undone.
          </p>
          <button
            className="btn"
            disabled={busy}
            onClick={() => {
              setConfirmReset(false);
              void perform({ reset: {} });
            }}
          >
            Confirm reset
          </button>
          <button
            className="btn"
            disabled={busy}
            onClick={() => setConfirmReset(false)}
          >
            Cancel
          </button>
        </div>
      )}
    </section>
  );
}
