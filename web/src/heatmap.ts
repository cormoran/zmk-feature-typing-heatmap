import {
  Request,
  Response,
  StatsResponse,
} from "./proto/cormoran/feature-typing-heatmap/feature_typing_heatmap";

// Keep this object stable: it is an input to useCustomSubsystem's call callback.
export const HEATMAP_CODEC = {
  encode: (request: Request) => Request.encode(request).finish(),
  decode: Response.decode,
};

export type HeatmapSnapshot = StatsResponse;
export type HeatmapCall = (request: Request) => Promise<Response | null>;

export function requireResponse(response: Response | null): Response {
  if (!response) throw new Error("The keyboard returned no response.");
  if (response.error) throw new Error(response.error.message);
  return response;
}

/** Read a coherent snapshot without polling while the user types. */
export async function readHeatmap(call: HeatmapCall): Promise<HeatmapSnapshot> {
  for (let attempt = 0; attempt < 3; attempt++) {
    let first: StatsResponse | undefined;
    const counts: number[] = [];
    let retry = false;
    do {
      const page = requireResponse(
        await call({ getStats: { offset: counts.length } })
      ).stats;
      if (!page)
        throw new Error("The keyboard returned an unexpected response.");
      if (first && page.generation !== first.generation) {
        retry = true;
        break;
      }
      first ??= page;
      if (
        page.offset !== counts.length ||
        page.positionCount !== first.positionCount ||
        page.counts.length > 16 ||
        counts.length + page.counts.length > page.positionCount ||
        (page.counts.length === 0 && counts.length < page.positionCount)
      ) {
        throw new Error("The keyboard returned an invalid statistics page.");
      }
      counts.push(...page.counts);
    } while (counts.length < first.positionCount);
    if (!retry && first) return { ...first, counts };
  }
  throw new Error(
    "Statistics changed while loading. Pause typing briefly, then press Refresh."
  );
}

export function exportHeatmap(snapshot: HeatmapSnapshot) {
  const blob = new Blob(
    [
      JSON.stringify(
        {
          exportedAt: new Date().toISOString(),
          ...snapshot,
          totalPresses: snapshot.counts.reduce((sum, count) => sum + count, 0),
        },
        null,
        2
      ),
    ],
    { type: "application/json" }
  );
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = "typing-heatmap.json";
  link.click();
  URL.revokeObjectURL(url);
}
